//! mirajazz input spike
//!
//! Two SAFE modes (neither sends `CRT DIS` / `initialize()`, so neither can
//! wedge the demo unit's display — see README):
//!
//!   * `enum`  (default) — enumerate every HID interface and print
//!                         vid/pid/usage-page/usage so we can confirm the
//!                         AKP05 control interface descriptor.
//!   * `input`           — connect via mirajazz, print firmware/serial, then
//!                         dump raw 512-byte input frames as hex for ~20s.
//!                         Press keys/turn encoders/touch during this window.
//!
//! There is deliberately NO output mode: reading the mirajazz source shows it
//! drives output with the SAME `CRT/BAT/STP` opcodes and `112x112 Rot180`
//! geometry the in-tree C++ already implements, and its `initialize()` sends
//! `CRT DIS` (the command our hardware notes say wedges this demo panel).
//! So output via mirajazz brings nothing new and only adds wedge risk.

use std::time::Duration;

use async_hid::HidBackend;
use futures_lite::StreamExt;
use mirajazz::{
    device::{Device, DeviceQuery, list_devices},
    error::MirajazzError,
};

// AKP05 (our demo unit) and Mirabox N4 — VID/PID + usage from opendeck-akp05's
// mappings.rs. usage_page 0xFF00 (65440), usage_id 1.
const AKP05_VID: u16 = 0x0300;
const AKP05_PID: u16 = 0x3004;
const N4_VID: u16 = 0x6603;
const N4_PID: u16 = 0x1007;

// NOTE: opendeck-akp05 guesses usage_page 0xFF00, but the live 0x0300:0x3004
// demo unit's vendor control interface is actually 0xFFA0 (confirmed via this
// spike's `enum` mode). usage_id 1 = control, 2 = secondary vendor iface.
const QUERIES: [DeviceQuery; 3] = [
    DeviceQuery::new(0xFFA0, 1, AKP05_VID, AKP05_PID),
    DeviceQuery::new(0xFFA0, 2, AKP05_VID, AKP05_PID),
    DeviceQuery::new(0xFF00, 1, N4_VID, N4_PID),
];

const PROTOCOL_VERSION: usize = 3;
const KEY_COUNT: usize = 15;
const ENCODER_COUNT: usize = 4;

#[tokio::main]
async fn main() {
    let mode = std::env::args().nth(1).unwrap_or_else(|| "enum".to_string());

    let result = match mode.as_str() {
        "enum" => enumerate().await,
        "input" => input_capture().await,
        other => {
            eprintln!("unknown mode '{other}'. use: enum | input");
            return;
        }
    };

    if let Err(e) = result {
        eprintln!("ERROR: {e}");
    }
}

/// List every HID interface, highlighting AKP05/N4 matches. No device is opened.
async fn enumerate() -> Result<(), MirajazzError> {
    println!("== enumerate: all HID interfaces (no open) ==");
    let backend = HidBackend::default();
    let mut stream = backend.enumerate().await?;
    let mut count = 0usize;
    while let Some(d) = stream.next().await {
        let interesting = (d.vendor_id == AKP05_VID && d.product_id == AKP05_PID)
            || (d.vendor_id == N4_VID && d.product_id == N4_PID);
        let marker = if interesting { " <== AKP05/N4" } else { "" };
        println!(
            "  vid={:04x} pid={:04x} usage_page={:#06x} usage_id={:#04x} name={:?} serial={:?}{}",
            d.vendor_id, d.product_id, d.usage_page, d.usage_id, d.name, d.serial_number, marker
        );
        count += 1;
    }
    println!("== {count} interfaces total ==");

    println!("\n== list_devices(QUERIES) — what mirajazz would actually connect to ==");
    let matched = list_devices(&QUERIES).await?;
    if matched.is_empty() {
        println!("  (none) — the QUERIES usage_page/usage_id likely don't match this unit.");
        println!("  Pick the AKP05 row above and update QUERIES in src/main.rs.");
    }
    for dev in &matched {
        println!(
            "  MATCH vid={:04x} pid={:04x} usage_page={:#06x} usage_id={:#04x}",
            dev.vendor_id, dev.product_id, dev.usage_page, dev.usage_id
        );
    }
    Ok(())
}

/// Connect via mirajazz and dump raw input frames. Never calls initialize().
async fn input_capture() -> Result<(), MirajazzError> {
    // Prefer the control interface (usage_id 1); fall back to whatever matched.
    let mut matched: Vec<_> = list_devices(&QUERIES).await?.into_iter().collect();
    matched.sort_by_key(|d| if d.usage_id == 1 { 0u8 } else { 1u8 });
    let dev = match matched.into_iter().next() {
        Some(d) => d,
        None => {
            eprintln!("No AKP05/N4 matched QUERIES. Run `enum` mode first to find the descriptor.");
            return Ok(());
        }
    };

    println!(
        "Connecting: vid={:04x} pid={:04x} usage_page={:#06x}",
        dev.vendor_id, dev.product_id, dev.usage_page
    );

    // connect() reads the firmware feature-report (id 0x01) but does NOT
    // initialize() — so no CRT DIS is sent here.
    let device = Device::connect(&dev, PROTOCOL_VERSION, KEY_COUNT, ENCODER_COUNT).await?;
    println!("  firmware = {:?}", device.firmware_version);
    println!("  serial   = {:?}", device.serial_number());

    let reader = device.get_reader(process_input);

    println!("\n== raw input capture (20s). Press keys / turn encoders / touch now ==");
    let deadline = Duration::from_secs(20);
    let start = tokio::time::Instant::now();
    let mut frames = 0usize;
    while start.elapsed() < deadline {
        match reader
            .raw_read_data_with_timeout(512, Duration::from_millis(500))
            .await?
        {
            Some(buf) => {
                frames += 1;
                let nonzero_len = buf.iter().rposition(|&b| b != 0).map_or(0, |p| p + 1);
                let head = &buf[..nonzero_len.max(16).min(buf.len())];
                let ack = buf.starts_with(&[65, 67, 75]); // "ACK"
                println!(
                    "  frame#{frames} len(nonzero)={nonzero_len} ack={ack} code=byte9={:#04x} state=byte10={:#04x}",
                    buf.get(9).copied().unwrap_or(0),
                    buf.get(10).copied().unwrap_or(0)
                );
                println!("    hex: {}", hex(head));
            }
            None => { /* timeout, no data — keep waiting */ }
        }
    }
    println!("\n== done: {frames} non-empty frames in 20s ==");
    if frames == 0 {
        println!("  ZERO input frames. On the 0x3004 demo unit this is the EXPECTED,");
        println!("  already-proven result (mirajazz hits the same wall). On a RETAIL");
        println!("  AKP05E/N4 this is where real codes would appear.");
    }
    Ok(())
}

/// mirajazz reader callback — reference decode (N4 mapping from opendeck-akp05).
/// We mostly rely on the raw hex dump above; this just keeps get_reader happy
/// if we later switch to the decoded `read()` path.
fn process_input(input: u8, _state: u8) -> Result<mirajazz::types::DeviceInput, MirajazzError> {
    println!("    process_input: code={input:#04x}");
    Ok(mirajazz::types::DeviceInput::NoData)
}

fn hex(bytes: &[u8]) -> String {
    bytes
        .iter()
        .map(|b| format!("{b:02x}"))
        .collect::<Vec<_>>()
        .join(" ")
}
