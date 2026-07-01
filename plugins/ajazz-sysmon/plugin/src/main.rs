// ajazz-sysmon — native cross-platform hardware monitor.
//
// Phase 1: CPU key. Phase 2: parameterized "Monitor" action (metric picker via a
// Property Inspector) + threshold colouring.
//
// Data: spawns the sibling `btop-metrics-helper` (which reuses btop's real
// collectors) and reads its streaming JSON. Render: a btop-styled tile — a
// gradient-filled area sparkline of recent history + the current value — pushed
// via set_image. The area graph keeps the metric's btop vertical gradient (the
// aesthetic); the value text is coloured green/amber/red by threshold where
// thresholds apply, else by the metric's value gradient.
use openaction::*;

use base64::Engine;
use serde::{Deserialize, Serialize};
use std::collections::{HashMap, VecDeque};
use std::sync::{Arc, Mutex, OnceLock};
use std::time::Duration;
use tokio::io::{AsyncBufReadExt, BufReader};
use tokio::process::Command;
use tokio::task::JoinHandle;

const FONT: &[u8] = include_bytes!("../assets/LiberationMono-Bold.ttf");
const HISTORY: usize = 120;

// ---------------- metrics ----------------

#[derive(Clone, Copy, PartialEq, Eq, Hash)]
enum Metric {
	Cpu,
	CpuTemp,
	Ram,
	NetDown,
	NetUp,
	Gpu,
	GpuTemp,
	Vram,
}

impl Metric {
	const ALL: [Metric; 8] = [
		Metric::Cpu,
		Metric::CpuTemp,
		Metric::Ram,
		Metric::NetDown,
		Metric::NetUp,
		Metric::Gpu,
		Metric::GpuTemp,
		Metric::Vram,
	];

	fn from_id(s: &str) -> Metric {
		match s {
			"cpu_temp" => Metric::CpuTemp,
			"ram" => Metric::Ram,
			"net_down" => Metric::NetDown,
			"net_up" => Metric::NetUp,
			"gpu" => Metric::Gpu,
			"gpu_temp" => Metric::GpuTemp,
			"vram" => Metric::Vram,
			_ => Metric::Cpu,
		}
	}

	fn label(self) -> &'static str {
		match self {
			Metric::Cpu => "CPU",
			Metric::CpuTemp => "TEMP",
			Metric::Ram => "RAM",
			Metric::NetDown => "NET DN",
			Metric::NetUp => "NET UP",
			Metric::Gpu => "GPU",
			Metric::GpuTemp => "GPU TMP",
			Metric::Vram => "VRAM",
		}
	}

	/// btop default-theme gradient stops (bottom/low -> mid -> top/high).
	fn grad(self) -> [(u8, u8, u8); 3] {
		match self {
			Metric::Cpu | Metric::Gpu => [(0x77, 0xca, 0x9b), (0xcb, 0xc0, 0x6c), (0xdc, 0x4c, 0x4c)],
			Metric::CpuTemp | Metric::GpuTemp => [(0x48, 0x97, 0xd4), (0x54, 0x74, 0xe8), (0xff, 0x40, 0xb6)],
			Metric::Ram | Metric::Vram => [(0x59, 0x2b, 0x26), (0xd9, 0x62, 0x6d), (0xff, 0x47, 0x69)],
			Metric::NetDown => [(0x29, 0x1f, 0x75), (0x4f, 0x43, 0xa3), (0xb0, 0xa9, 0xde)],
			Metric::NetUp => [(0x62, 0x06, 0x65), (0x7d, 0x41, 0x80), (0xdc, 0xaf, 0xde)],
		}
	}

	/// Fixed 0..scale for %/°C metrics; None = auto-range (network throughput).
	fn scale_max(self) -> Option<f32> {
		match self {
			Metric::Cpu | Metric::Ram | Metric::CpuTemp | Metric::Gpu | Metric::GpuTemp | Metric::Vram => Some(100.0),
			Metric::NetDown | Metric::NetUp => None,
		}
	}

	/// Default (warn, crit) thresholds; None = thresholds not meaningful.
	fn default_thresholds(self) -> Option<(f32, f32)> {
		match self {
			Metric::Cpu | Metric::Ram => Some((70.0, 90.0)),
			Metric::CpuTemp => Some((70.0, 85.0)),
			// 100% GPU utilisation is normal under load — no thresholds by default.
			Metric::Gpu => None,
			Metric::GpuTemp => Some((75.0, 90.0)),
			Metric::Vram => Some((80.0, 95.0)),
			Metric::NetDown | Metric::NetUp => None,
		}
	}

	fn format(self, v: f32) -> String {
		match self {
			Metric::Cpu | Metric::Ram | Metric::Gpu | Metric::Vram => format!("{v:.0}%"),
			Metric::CpuTemp | Metric::GpuTemp => format!("{v:.0}\u{00b0}C"),
			Metric::NetDown | Metric::NetUp => fmt_rate(v),
		}
	}
}

fn fmt_rate(bps: f32) -> String {
	if bps >= 1_000_000.0 {
		format!("{:.1}M", bps / 1_000_000.0)
	} else if bps >= 1_000.0 {
		format!("{:.0}K", bps / 1_000.0)
	} else {
		format!("{bps:.0}B")
	}
}

// ---------------- shared metrics state ----------------

#[derive(Default)]
struct SharedMetrics {
	hist: HashMap<Metric, VecDeque<f32>>,
}

fn metrics() -> &'static Mutex<SharedMetrics> {
	static M: OnceLock<Mutex<SharedMetrics>> = OnceLock::new();
	M.get_or_init(|| Mutex::new(SharedMetrics::default()))
}

fn push_metric(m: &mut SharedMetrics, k: Metric, v: f32) {
	let dq = m.hist.entry(k).or_default();
	dq.push_back(v);
	while dq.len() > HISTORY {
		dq.pop_front();
	}
}

/// Latest value + a copy of the history for `metric`.
fn metric_series(metric: Metric) -> (f32, VecDeque<f32>) {
	let m = metrics().lock().unwrap();
	let dq = m.hist.get(&metric).cloned().unwrap_or_default();
	(dq.back().copied().unwrap_or(0.0), dq)
}

#[derive(Deserialize)]
struct Snapshot {
	cpu: CpuBlock,
	mem: MemBlock,
	net: NetBlock,
	/// Absent on older helpers; empty when no GPU is detected.
	#[serde(default)]
	gpu: Vec<GpuBlock>,
}
#[derive(Deserialize)]
struct CpuBlock {
	percent: f64,
	temp_c: f64,
}
#[derive(Deserialize)]
struct MemBlock {
	percent: f64,
}
#[derive(Deserialize)]
struct NetBlock {
	down_bytes_s: f64,
	up_bytes_s: f64,
}
/// One GPU from the helper's `gpu` array. Extra fields ("name", "supported",
/// clocks, …) are intentionally ignored; missing fields default to 0.
#[derive(Deserialize, Default)]
#[serde(default)]
struct GpuBlock {
	util_percent: f64,
	temp_c: f64,
	vram_used_bytes: f64,
	vram_total_bytes: f64,
	#[allow(dead_code)]
	power_w: f64,
}

async fn run_helper() {
	let helper = match std::env::current_exe()
		.ok()
		.and_then(|p| p.parent().map(|d| d.join("btop-metrics-helper")))
	{
		Some(h) => h,
		None => return,
	};
	let mut child = match Command::new(&helper).stdout(std::process::Stdio::piped()).spawn() {
		Ok(c) => c,
		Err(e) => {
			eprintln!("ajazz-sysmon: cannot start helper {helper:?}: {e}");
			return;
		}
	};
	let Some(out) = child.stdout.take() else { return };
	let mut lines = BufReader::new(out).lines();
	while let Ok(Some(line)) = lines.next_line().await {
		if let Ok(s) = serde_json::from_str::<Snapshot>(&line) {
			let mut m = metrics().lock().unwrap();
			push_metric(&mut m, Metric::Cpu, s.cpu.percent as f32);
			push_metric(&mut m, Metric::CpuTemp, s.cpu.temp_c as f32);
			push_metric(&mut m, Metric::Ram, s.mem.percent as f32);
			push_metric(&mut m, Metric::NetDown, s.net.down_bytes_s as f32);
			push_metric(&mut m, Metric::NetUp, s.net.up_bytes_s as f32);
			// Phase 3: first GPU only; multi-GPU selection is a later phase.
			if let Some(g) = s.gpu.first() {
				push_metric(&mut m, Metric::Gpu, g.util_percent as f32);
				push_metric(&mut m, Metric::GpuTemp, g.temp_c as f32);
				let vram_pct = if g.vram_total_bytes > 0.0 {
					(g.vram_used_bytes / g.vram_total_bytes * 100.0) as f32
				} else {
					0.0
				};
				push_metric(&mut m, Metric::Vram, vram_pct);
			}
		}
	}
}

// ---------------- btop-style rendering ----------------

fn lerp(a: (u8, u8, u8), b: (u8, u8, u8), t: f32) -> (u8, u8, u8) {
	let t = t.clamp(0.0, 1.0);
	let l = |x: u8, y: u8| (x as f32 + (y as f32 - x as f32) * t).round() as u8;
	(l(a.0, b.0), l(a.1, b.1), l(a.2, b.2))
}

/// Two-segment linear RGB interpolation, matching btop's `generateGradients()`.
fn grad_at(stops: [(u8, u8, u8); 3], t: f32) -> (u8, u8, u8) {
	if t < 0.5 {
		lerp(stops[0], stops[1], t * 2.0)
	} else {
		lerp(stops[1], stops[2], (t - 0.5) * 2.0)
	}
}

// Semantic threshold colours (btop cpu green / tan / red).
const OK: (u8, u8, u8) = (0x77, 0xca, 0x9b);
const WARN: (u8, u8, u8) = (0xcb, 0xc0, 0x6c);
const CRIT: (u8, u8, u8) = (0xdc, 0x4c, 0x4c);

fn render_tile(metric: Metric, history: &VecDeque<f32>, value: f32, warn: Option<f32>, crit: Option<f32>) -> Option<Vec<u8>> {
	use tiny_skia::*;

	let size = 144u32;
	let (w, h) = (size as f32, size as f32);
	let mut pm = Pixmap::new(size, size)?;
	pm.fill(Color::from_rgba8(0, 0, 0, 255));

	// Graph scale: fixed for %/°C, auto-range (peak of history) for network.
	let scale = metric
		.scale_max()
		.unwrap_or_else(|| history.iter().cloned().fold(1.0_f32, f32::max));
	let norm = |v: f32| (v / scale).clamp(0.0, 1.0);

	let stops = metric.grad();
	if history.len() >= 2 {
		let n = history.len();
		let mut pb = PathBuilder::new();
		pb.move_to(0.0, h);
		for (i, v) in history.iter().enumerate() {
			let x = i as f32 / (n as f32 - 1.0) * w;
			let y = h - norm(*v) * h;
			pb.line_to(x, y);
		}
		pb.line_to(w, h);
		pb.close();
		if let Some(path) = pb.finish() {
			let g = vec![
				GradientStop::new(0.0, Color::from_rgba8(stops[2].0, stops[2].1, stops[2].2, 235)),
				GradientStop::new(0.5, Color::from_rgba8(stops[1].0, stops[1].1, stops[1].2, 225)),
				GradientStop::new(1.0, Color::from_rgba8(stops[0].0, stops[0].1, stops[0].2, 215)),
			];
			if let Some(shader) = LinearGradient::new(
				Point::from_xy(0.0, 0.0),
				Point::from_xy(0.0, h),
				g,
				SpreadMode::Pad,
				Transform::identity(),
			) {
				let mut paint = Paint::default();
				paint.shader = shader;
				paint.anti_alias = true;
				pm.fill_path(&path, &paint, FillRule::Winding, Transform::identity(), None);
			}
		}
	}

	// Value colour: threshold bands where they apply, else the value gradient.
	let vcol = match (warn, crit) {
		(Some(wv), Some(cv)) => {
			if value >= cv {
				CRIT
			} else if value >= wv {
				WARN
			} else {
				OK
			}
		}
		_ => grad_at(stops, norm(value)),
	};

	draw_text(&mut pm, metric.label(), 8.0, 30.0, 20.0, (0xcc, 0xcc, 0xcc));
	draw_text(&mut pm, &metric.format(value), 8.0, h - 16.0, 40.0, vcol);

	pm.encode_png().ok()
}

fn draw_text(pm: &mut tiny_skia::Pixmap, text: &str, x: f32, baseline: f32, px: f32, col: (u8, u8, u8)) {
	use ab_glyph::{Font, FontRef, PxScale, ScaleFont};

	let font = match FontRef::try_from_slice(FONT) {
		Ok(f) => f,
		Err(_) => return,
	};
	let scale = PxScale::from(px);
	let sf = font.as_scaled(scale);
	let (width, height) = (pm.width() as i32, pm.height() as i32);
	let data = pm.data_mut();

	let mut caret = x;
	for ch in text.chars() {
		let gid = font.glyph_id(ch);
		let glyph = gid.with_scale_and_position(scale, ab_glyph::point(caret, baseline));
		if let Some(og) = font.outline_glyph(glyph) {
			let bb = og.px_bounds();
			og.draw(|gx, gy, cov| {
				let px_x = bb.min.x as i32 + gx as i32;
				let px_y = bb.min.y as i32 + gy as i32;
				if px_x >= 0 && px_x < width && px_y >= 0 && px_y < height {
					let idx = ((px_y * width + px_x) * 4) as usize;
					let a = cov.clamp(0.0, 1.0);
					let inv = 1.0 - a;
					data[idx] = (col.0 as f32 * a + data[idx] as f32 * inv) as u8;
					data[idx + 1] = (col.1 as f32 * a + data[idx + 1] as f32 * inv) as u8;
					data[idx + 2] = (col.2 as f32 * a + data[idx + 2] as f32 * inv) as u8;
					data[idx + 3] = (255.0 * a + data[idx + 3] as f32 * inv) as u8;
				}
			});
		}
		caret += sf.h_advance(gid);
	}
}

// ---------------- settings + render loop ----------------

#[derive(Serialize, Deserialize, Clone, Default)]
#[serde(default)]
struct MonitorSettings {
	metric: String,
	warn: Option<f32>,
	crit: Option<f32>,
}

impl MonitorSettings {
	fn resolve(&self) -> (Metric, Option<f32>, Option<f32>) {
		let m = Metric::from_id(&self.metric);
		let dflt = m.default_thresholds();
		let warn = self.warn.or(dflt.map(|d| d.0));
		let crit = self.crit.or(dflt.map(|d| d.1));
		(m, warn, crit)
	}
}

/// Per-instance settings, shared with the render loop and updated by
/// did_receive_settings so a metric change reflects live.
fn settings_map() -> &'static Mutex<HashMap<InstanceId, MonitorSettings>> {
	static S: OnceLock<Mutex<HashMap<InstanceId, MonitorSettings>>> = OnceLock::new();
	S.get_or_init(|| Mutex::new(HashMap::new()))
}

/// Spawn the 1 Hz render loop for one instance. `fixed` pins the metric (the CPU
/// preset); otherwise the metric is read from the live per-instance settings.
fn spawn_render(inst: Arc<Instance>, id: InstanceId, fixed: Option<Metric>) -> JoinHandle<()> {
	tokio::spawn(async move {
		loop {
			let (metric, warn, crit) = match fixed {
				Some(m) => (m, m.default_thresholds().map(|d| d.0), m.default_thresholds().map(|d| d.1)),
				None => settings_map().lock().unwrap().get(&id).cloned().unwrap_or_default().resolve(),
			};
			let (val, hist) = metric_series(metric);
			if let Some(png) = render_tile(metric, &hist, val, warn, crit) {
				let uri = format!("data:image/png;base64,{}", base64::engine::general_purpose::STANDARD.encode(&png));
				if inst.set_image(Some(uri), None).await.is_err() {
					break;
				}
			}
			tokio::time::sleep(Duration::from_millis(1000)).await;
		}
	})
}

// ---------------- actions ----------------

#[derive(Default)]
struct Tasks(Mutex<HashMap<InstanceId, JoinHandle<()>>>);

impl Tasks {
	fn insert(&self, id: InstanceId, task: JoinHandle<()>) {
		if let Ok(mut m) = self.0.lock() {
			if let Some(old) = m.insert(id, task) {
				old.abort();
			}
		}
	}
	fn remove(&self, id: &str) {
		if let Ok(mut m) = self.0.lock() {
			if let Some(t) = m.remove(id) {
				t.abort();
			}
		}
	}
}

/// The CPU preset (metric fixed, no Property Inspector).
#[derive(Default)]
struct CpuAction {
	tasks: Tasks,
}

#[async_trait]
impl Action for CpuAction {
	const UUID: ActionUuid = "com.ajazz.sysmon2.cpu";
	type Settings = MonitorSettings;

	async fn will_appear(&self, instance: &Instance, _s: &Self::Settings) -> OpenActionResult<()> {
		let id = instance.instance_id.clone();
		if let Some(inst) = get_instance(id.clone()).await {
			self.tasks.insert(id.clone(), spawn_render(inst, id, Some(Metric::Cpu)));
		}
		Ok(())
	}
	async fn will_disappear(&self, instance: &Instance, _s: &Self::Settings) -> OpenActionResult<()> {
		self.tasks.remove(&instance.instance_id);
		Ok(())
	}
}

/// The parameterized Monitor action (metric picker + thresholds via the PI).
#[derive(Default)]
struct MonitorAction {
	tasks: Tasks,
}

#[async_trait]
impl Action for MonitorAction {
	const UUID: ActionUuid = "com.ajazz.sysmon2.monitor";
	type Settings = MonitorSettings;

	async fn will_appear(&self, instance: &Instance, settings: &Self::Settings) -> OpenActionResult<()> {
		let id = instance.instance_id.clone();
		settings_map().lock().unwrap().insert(id.clone(), settings.clone());
		if let Some(inst) = get_instance(id.clone()).await {
			self.tasks.insert(id.clone(), spawn_render(inst, id, None));
		}
		Ok(())
	}

	async fn did_receive_settings(&self, instance: &Instance, settings: &Self::Settings) -> OpenActionResult<()> {
		settings_map()
			.lock()
			.unwrap()
			.insert(instance.instance_id.clone(), settings.clone());
		Ok(())
	}

	async fn will_disappear(&self, instance: &Instance, _s: &Self::Settings) -> OpenActionResult<()> {
		self.tasks.remove(&instance.instance_id);
		settings_map().lock().unwrap().remove(&instance.instance_id);
		Ok(())
	}
}

/// The GPU preset: defaults to GPU utilisation but keeps the Property
/// Inspector, so the metric (gpu / gpu_temp / vram) and thresholds stay
/// overridable per instance.
#[derive(Default)]
struct GpuAction {
	tasks: Tasks,
}

/// Settings with the metric defaulted to `default` when unset (preset actions
/// that still honour their PI).
fn settings_with_default(settings: &MonitorSettings, default: &str) -> MonitorSettings {
	let mut s = settings.clone();
	if s.metric.is_empty() {
		s.metric = default.into();
	}
	s
}

#[async_trait]
impl Action for GpuAction {
	const UUID: ActionUuid = "com.ajazz.sysmon2.gpu";
	type Settings = MonitorSettings;

	async fn will_appear(&self, instance: &Instance, settings: &Self::Settings) -> OpenActionResult<()> {
		let id = instance.instance_id.clone();
		settings_map()
			.lock()
			.unwrap()
			.insert(id.clone(), settings_with_default(settings, "gpu"));
		if let Some(inst) = get_instance(id.clone()).await {
			self.tasks.insert(id.clone(), spawn_render(inst, id, None));
		}
		Ok(())
	}

	async fn did_receive_settings(&self, instance: &Instance, settings: &Self::Settings) -> OpenActionResult<()> {
		settings_map()
			.lock()
			.unwrap()
			.insert(instance.instance_id.clone(), settings_with_default(settings, "gpu"));
		Ok(())
	}

	async fn will_disappear(&self, instance: &Instance, _s: &Self::Settings) -> OpenActionResult<()> {
		self.tasks.remove(&instance.instance_id);
		settings_map().lock().unwrap().remove(&instance.instance_id);
		Ok(())
	}
}

#[tokio::main]
async fn main() -> OpenActionResult<()> {
	// Dev aid: `--render-test <metric> <out.png>` renders a sample tile and exits.
	let argv: Vec<String> = std::env::args().collect();
	if argv.get(1).map(String::as_str) == Some("--render-test") {
		let metric = Metric::from_id(argv.get(2).map(String::as_str).unwrap_or("cpu"));
		let out = argv.get(3).cloned().unwrap_or_else(|| "/tmp/sysmon-tile.png".into());
		let mut hist = VecDeque::new();
		for i in 0..120 {
			let t = i as f32 / 119.0;
			let base = match metric {
				Metric::CpuTemp | Metric::GpuTemp => 45.0 + 40.0 * (t * 5.0).sin().abs(),
				Metric::NetDown | Metric::NetUp => 200_000.0 * (t * 6.0).sin().abs() + 50_000.0 * t,
				_ => 30.0 + 55.0 * (t * 6.0).sin().abs() + 10.0 * t,
			};
			hist.push_back(base);
		}
		let (_, w, c) = MonitorSettings { metric: argv.get(2).cloned().unwrap_or_default(), warn: None, crit: None }.resolve();
		if let Some(png) = render_tile(metric, &hist, *hist.back().unwrap(), w, c) {
			std::fs::write(&out, png).ok();
			eprintln!("wrote {out}");
		}
		let _ = Metric::ALL;
		return Ok(());
	}

	tokio::spawn(run_helper());
	register_action(CpuAction::default()).await;
	register_action(MonitorAction::default()).await;
	register_action(GpuAction::default()).await;
	run(std::env::args().collect()).await
}

#[cfg(test)]
mod tests {
	use super::*;

	#[test]
	fn snapshot_parses_with_gpu() {
		let line = r#"{"cpu":{"percent":12.5,"temp_c":48.0},"mem":{"percent":40.2},
			"net":{"down_bytes_s":1024.0,"up_bytes_s":256.0},
			"gpu":[{"name":"Radeon","util_percent":33,"temp_c":61,"vram_used_bytes":2147483648,
			"vram_total_bytes":8589934592,"power_w":95.5,"core_clock_mhz":2100,"mem_clock_mhz":1750,
			"supported":{"util":true,"temp":true,"vram":true,"power":true,"core_clock":true,"mem_clock":true}}]}"#;
		let s: Snapshot = serde_json::from_str(line).expect("gpu snapshot must parse");
		let g = s.gpu.first().expect("one gpu");
		assert_eq!(g.util_percent, 33.0);
		assert_eq!(g.temp_c, 61.0);
		assert_eq!(g.vram_used_bytes / g.vram_total_bytes * 100.0, 25.0);
	}

	#[test]
	fn snapshot_parses_without_gpu() {
		// Older helpers omit the "gpu" key entirely.
		let line = r#"{"cpu":{"percent":12.5,"temp_c":48.0},"mem":{"percent":40.2},
			"net":{"down_bytes_s":1024.0,"up_bytes_s":256.0}}"#;
		let s: Snapshot = serde_json::from_str(line).expect("gpu-less snapshot must parse");
		assert!(s.gpu.is_empty());
	}

	#[test]
	fn snapshot_parses_with_sparse_gpu_fields() {
		// "supported"/clock fields missing, partial metrics — must not fail.
		let line = r#"{"cpu":{"percent":1,"temp_c":2},"mem":{"percent":3},
			"net":{"down_bytes_s":4,"up_bytes_s":5},"gpu":[{"name":"x","util_percent":7}]}"#;
		let s: Snapshot = serde_json::from_str(line).expect("sparse gpu must parse");
		assert_eq!(s.gpu[0].util_percent, 7.0);
		assert_eq!(s.gpu[0].vram_total_bytes, 0.0);
	}
}
