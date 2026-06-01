//! streamdock-host — out-of-process Rust sidecar (mirajazz) for the AKP05/N4
//! Stream Dock family. Newline-delimited JSON over stdin/stdout. See Cargo.toml
//! for the protocol summary.
//!
//! Slice 1 scope: announce connected devices + firmware, stream raw input
//! events, `ping`, and `set_brightness` (gated behind --allow-output because
//! it triggers mirajazz `initialize()` → `CRT DIS`, the wedge-risk command).

use std::{collections::HashMap, sync::Arc, time::Duration};

use mirajazz::{
    device::{Device, DeviceQuery, list_devices},
    error::MirajazzError,
    types::DeviceInput,
};
use tokio::{
    io::{AsyncBufReadExt, BufReader},
    sync::Mutex,
};

// AKP05 demo unit's vendor control interface is usage_page 0xFFA0 (confirmed by
// the enum spike — opendeck-akp05's 0xFF00 guess is wrong for this hardware).
const QUERIES: [DeviceQuery; 3] = [
    DeviceQuery::new(0xFFA0, 1, 0x0300, 0x3004), // AKP05 control
    DeviceQuery::new(0xFFA0, 2, 0x0300, 0x3004), // AKP05 secondary
    DeviceQuery::new(0xFF00, 1, 0x6603, 0x1007), // Mirabox N4 (provisional)
];

const PROTOCOL_VERSION: usize = 3;
const KEY_COUNT: usize = 15;
const ENCODER_COUNT: usize = 4;

type DeviceMap = Arc<Mutex<HashMap<String, Arc<Device>>>>;

/// Emit one JSON line to stdout. `println!` locks stdout, so tasks don't interleave.
fn emit(obj: serde_json::Value) {
    println!("{obj}");
}

fn noop_process(_input: u8, _state: u8) -> Result<DeviceInput, MirajazzError> {
    Ok(DeviceInput::NoData)
}

#[tokio::main]
async fn main() {
    let allow_output = std::env::args().any(|a| a == "--allow-output");

    let devices: DeviceMap = Arc::new(Mutex::new(HashMap::new()));

    let matched = match list_devices(&QUERIES).await {
        Ok(set) => set,
        Err(e) => {
            emit(serde_json::json!({"event": "error", "msg": format!("enumerate: {e}")}));
            return;
        }
    };

    // One control interface per physical unit (usage_id 1). Skip the secondary.
    for dev in matched.into_iter().filter(|d| d.usage_id == 1) {
        match Device::connect(&dev, PROTOCOL_VERSION, KEY_COUNT, ENCODER_COUNT).await {
            Ok(device) => {
                let device = Arc::new(device);
                let serial = device.serial_number().clone();
                emit(serde_json::json!({
                    "event": "connected",
                    "serial": serial,
                    "vid": device.vid,
                    "pid": device.pid,
                    "firmware": device.firmware_version.clone(),
                }));

                // Per-device input reader. Uses raw frames (no initialize/DIS).
                let reader = device.get_reader(noop_process);
                let serial_for_task = serial.clone();
                tokio::spawn(async move {
                    loop {
                        match reader
                            .raw_read_data_with_timeout(512, Duration::from_millis(500))
                            .await
                        {
                            Ok(Some(buf)) => {
                                let hex: String =
                                    buf.iter().take(16).map(|b| format!("{b:02x}")).collect();
                                emit(serde_json::json!({
                                    "event": "input",
                                    "serial": serial_for_task,
                                    "code": buf.get(9).copied().unwrap_or(0),
                                    "state": buf.get(10).copied().unwrap_or(0),
                                    "raw": hex,
                                }));
                            }
                            Ok(None) => { /* idle timeout */ }
                            Err(e) => {
                                emit(serde_json::json!({
                                    "event": "device_error",
                                    "serial": serial_for_task,
                                    "msg": format!("{e}"),
                                }));
                                break;
                            }
                        }
                    }
                });

                devices.lock().await.insert(serial, device);
            }
            Err(e) => emit(serde_json::json!({"event": "error", "msg": format!("connect: {e}")})),
        }
    }

    emit(serde_json::json!({
        "event": "ready",
        "device_count": devices.lock().await.len(),
        "output_allowed": allow_output,
    }));

    // Command loop: one JSON object per stdin line.
    let mut lines = BufReader::new(tokio::io::stdin()).lines();
    while let Ok(Some(line)) = lines.next_line().await {
        let line = line.trim();
        if line.is_empty() {
            continue;
        }
        let cmd: serde_json::Value = match serde_json::from_str(line) {
            Ok(v) => v,
            Err(e) => {
                emit(serde_json::json!({"event": "error", "msg": format!("bad json: {e}")}));
                continue;
            }
        };

        match cmd.get("cmd").and_then(|c| c.as_str()) {
            Some("ping") => emit(serde_json::json!({"event": "pong"})),
            Some("set_brightness") => {
                handle_set_brightness(&devices, &cmd, allow_output).await;
            }
            other => emit(serde_json::json!({
                "event": "error",
                "msg": format!("unknown cmd: {other:?}"),
            })),
        }
    }
}

async fn handle_set_brightness(devices: &DeviceMap, cmd: &serde_json::Value, allow_output: bool) {
    if !allow_output {
        emit(serde_json::json!({
            "event": "error",
            "msg": "output disabled (start with --allow-output); set_brightness sends CRT DIS",
        }));
        return;
    }
    let serial = cmd.get("serial").and_then(|s| s.as_str()).unwrap_or("");
    let percent = cmd.get("percent").and_then(|p| p.as_u64()).unwrap_or(50) as u8;

    let device = devices.lock().await.get(serial).cloned();
    match device {
        Some(device) => match device.set_brightness(percent).await {
            Ok(()) => emit(serde_json::json!({"event": "ok", "cmd": "set_brightness", "serial": serial})),
            Err(e) => emit(serde_json::json!({"event": "error", "msg": format!("set_brightness: {e}")})),
        },
        None => emit(serde_json::json!({"event": "error", "msg": format!("no device {serial}")})),
    }
}
