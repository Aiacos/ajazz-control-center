# System Monitor (btop) — bundled plugin guide

The **System Monitor (btop)** plugin (`com.ajazz.sysmon2`) ships with AJAZZ
Control Center and renders live machine metrics as btop-styled sparkline tiles
on Stream Dock keys, dials and touch-strip zones. It is seeded into your
plugins directory automatically on first run; deleting it from the Plugins tab
is respected (it will not re-seed).

## Actions

| Action          | What it does                                                                                                 |
| --------------- | ------------------------------------------------------------------------------------------------------------ |
| **Monitor**     | The parameterized tile: pick any metric below in the property inspector, plus optional warn/crit thresholds. |
| **CPU**         | Fixed CPU-usage tile (a preconfigured Monitor).                                                              |
| **GPU Monitor** | Fixed GPU-utilisation tile.                                                                                  |

On a **dial**, bind Monitor and: **rotate** to switch the displayed metric
(wraps through the full list, choice persists), **press** to cycle. The
touch-strip zone above the dial shows the same tile at strip resolution.

## Metrics

| Id                                        | Tile label                     | Source                                              | Notes                                                                                                                      |
| ----------------------------------------- | ------------------------------ | --------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------- |
| `cpu`                                     | CPU                            | btop collector                                      | percent, warn/crit 70/90                                                                                                   |
| `cpu_temp`                                | TEMP                           | btop sensors                                        | °C, warn/crit 70/85                                                                                                        |
| `ram`                                     | RAM                            | btop                                                | percent, warn/crit 70/90                                                                                                   |
| `disk`                                    | DISK                           | btop                                                | root filesystem used %, warn/crit 85/95                                                                                    |
| `net_down` / `net_up`                     | NET DN / NET UP                | btop `Net::collect`                                 | auto-ranged B/s (K/M)                                                                                                      |
| `gpu` / `gpu_temp` / `vram` / `gpu_power` | GPU / GPU TMP / VRAM / GPU PWR | NVML (dlopen) / ROCm SMI / amdgpu sysfs / Intel PMU | first GPU; NVML "used" VRAM includes driver-reserved memory                                                                |
| `ping`                                    | PING                           | plugin-side TCP connect to `1.1.1.1:443` every 2 s  | SYN/ACK timing ≈ ICMP trend, no privileges needed; timeouts pin at 1500 ms so outages spike the graph; warn/crit 60/150 ms |
| `battery`                                 | BAT                            | `/sys/class/power_supply`                           | percent; hidden on desktops without a battery                                                                              |
| `uptime`                                  | UP                             | `/proc/uptime`                                      | compact label (`3d 4h`, `4h05`, `12m`)                                                                                     |

Thresholds colour the current value green/amber/red; leave the fields blank
for the per-metric defaults above.

## How it works

The plugin (`plugins/ajazz-sysmon/`, Rust) spawns a sibling
`btop-metrics-helper` binary — five btop collector translation units compiled
standalone (Apache-2.0, credited in `NOTICE`) — which emits one JSON snapshot
per second on stdout. The plugin keeps a 120-sample history per metric and
renders tiles with tiny-skia using btop's default-theme gradients. Ping is
sampled by the plugin itself (tokio task); battery/uptime are plain
sysfs/procfs reads on the helper's beat.

Linux-only today: the Windows temperature/GPU tier is hardware-gated
(Phase 5, needs a Windows machine + LibreHardwareMonitor), and macOS sensors
are a later phase. The bundle builds automatically with the app when `cargo`
is present (`plugins/CMakeLists.txt`) and installs into
`<datadir>/ajazz-control-center/bundled-plugins/`.
