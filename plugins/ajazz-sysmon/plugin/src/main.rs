// ajazz-sysmon — native cross-platform hardware monitor (Phase 1: CPU key).
//
// Data: spawns the sibling `btop-metrics-helper` (which reuses btop's real
// collectors) and reads its streaming JSON. Render: a btop-styled tile — a
// gradient-filled area sparkline of recent CPU history + the current value,
// coloured by btop's default-theme "cpu" gradient — pushed via set_image.
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

// btop default-theme "cpu" gradient stops (bottom -> mid -> top).
const CPU_START: (u8, u8, u8) = (0x77, 0xca, 0x9b); // green (low)
const CPU_MID: (u8, u8, u8) = (0xcb, 0xc0, 0x6c); // tan
const CPU_END: (u8, u8, u8) = (0xdc, 0x4c, 0x4c); // red (high)

// ---------------- shared metrics state ----------------

#[derive(Default)]
struct SharedMetrics {
	cpu: f32,
	history: VecDeque<f32>,
}

fn metrics() -> &'static Arc<Mutex<SharedMetrics>> {
	static M: OnceLock<Arc<Mutex<SharedMetrics>>> = OnceLock::new();
	M.get_or_init(|| Arc::new(Mutex::new(SharedMetrics::default())))
}

#[derive(Deserialize)]
struct Snapshot {
	cpu: CpuBlock,
}
#[derive(Deserialize)]
struct CpuBlock {
	percent: f64,
}

/// Spawn the sibling metrics helper and pump its JSON stream into shared state.
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
		if let Ok(snap) = serde_json::from_str::<Snapshot>(&line) {
			let cpu = snap.cpu.percent as f32;
			let mut m = metrics().lock().unwrap();
			m.cpu = cpu;
			m.history.push_back(cpu);
			while m.history.len() > HISTORY {
				m.history.pop_front();
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
fn cpu_color(t: f32) -> (u8, u8, u8) {
	if t < 0.5 {
		lerp(CPU_START, CPU_MID, t * 2.0)
	} else {
		lerp(CPU_MID, CPU_END, (t - 0.5) * 2.0)
	}
}

fn render_cpu_tile(history: &VecDeque<f32>, value: f32) -> Option<Vec<u8>> {
	use tiny_skia::*;

	let size = 144u32;
	let (w, h) = (size as f32, size as f32);
	let mut pm = Pixmap::new(size, size)?;
	pm.fill(Color::from_rgba8(0, 0, 0, 255)); // btop near-black bg

	// Gradient-filled area sparkline of the CPU history.
	if history.len() >= 2 {
		let n = history.len();
		let mut pb = PathBuilder::new();
		pb.move_to(0.0, h);
		for (i, v) in history.iter().enumerate() {
			let x = i as f32 / (n as f32 - 1.0) * w;
			let y = h - ((*v).clamp(0.0, 100.0) / 100.0) * h;
			pb.line_to(x, y);
		}
		pb.line_to(w, h);
		pb.close();
		if let Some(path) = pb.finish() {
			// Vertical wash: red at the top (y=0), green at the bottom (y=h).
			let stops = vec![
				GradientStop::new(0.0, Color::from_rgba8(CPU_END.0, CPU_END.1, CPU_END.2, 235)),
				GradientStop::new(0.5, Color::from_rgba8(CPU_MID.0, CPU_MID.1, CPU_MID.2, 225)),
				GradientStop::new(1.0, Color::from_rgba8(CPU_START.0, CPU_START.1, CPU_START.2, 215)),
			];
			if let Some(shader) = LinearGradient::new(
				Point::from_xy(0.0, 0.0),
				Point::from_xy(0.0, h),
				stops,
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

	// "CPU" label (btop main_fg) + the big value coloured by the cpu gradient.
	draw_text(&mut pm, "CPU", 8.0, 30.0, 22.0, (0xcc, 0xcc, 0xcc));
	let vcol = cpu_color(value / 100.0);
	draw_text(&mut pm, &format!("{value:.0}%"), 8.0, h - 16.0, 44.0, vcol);

	pm.encode_png().ok()
}

/// Draw `text` with baseline origin at (x, baseline), source-over onto the
/// premultiplied RGBA pixmap buffer.
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

// ---------------- openaction plumbing ----------------

#[derive(Serialize, Deserialize, Default)]
struct CpuSettings {}

#[derive(Default)]
struct CpuAction {
	tasks: Mutex<HashMap<InstanceId, JoinHandle<()>>>,
}

#[async_trait]
impl Action for CpuAction {
	const UUID: ActionUuid = "com.ajazz.sysmon2.cpu";
	type Settings = CpuSettings;

	async fn will_appear(&self, instance: &Instance, _s: &Self::Settings) -> OpenActionResult<()> {
		let id = instance.instance_id.clone();
		let Some(inst) = get_instance(id.clone()).await else { return Ok(()) };

		let task = tokio::spawn(async move {
			loop {
				let (val, hist) = {
					let m = metrics().lock().unwrap();
					(m.cpu, m.history.clone())
				};
				if let Some(png) = render_cpu_tile(&hist, val) {
					let uri = format!(
						"data:image/png;base64,{}",
						base64::engine::general_purpose::STANDARD.encode(&png)
					);
					if inst.set_image(Some(uri), None).await.is_err() {
						break;
					}
				}
				tokio::time::sleep(Duration::from_millis(1000)).await;
			}
		});

		if let Ok(mut map) = self.tasks.lock() {
			if let Some(old) = map.insert(id, task) {
				old.abort();
			}
		}
		Ok(())
	}

	async fn will_disappear(&self, instance: &Instance, _s: &Self::Settings) -> OpenActionResult<()> {
		if let Ok(mut map) = self.tasks.lock() {
			if let Some(task) = map.remove(&instance.instance_id) {
				task.abort();
			}
		}
		Ok(())
	}
}

#[tokio::main]
async fn main() -> OpenActionResult<()> {
	// Dev aid: `--render-test <out.png>` renders a sample tile and exits, so the
	// btop aesthetic can be eyeballed without the device pipeline.
	let argv: Vec<String> = std::env::args().collect();
	if argv.get(1).map(String::as_str) == Some("--render-test") {
		let mut hist = VecDeque::new();
		for i in 0..120 {
			let t = i as f32 / 119.0;
			hist.push_back((30.0 + 55.0 * (t * 6.0).sin().abs() + 10.0 * t).min(100.0));
		}
		let out = argv.get(2).cloned().unwrap_or_else(|| "/tmp/sysmon-tile.png".into());
		if let Some(png) = render_cpu_tile(&hist, *hist.back().unwrap()) {
			std::fs::write(&out, png).ok();
			eprintln!("wrote {out}");
		}
		return Ok(());
	}

	tokio::spawn(run_helper());
	register_action(CpuAction::default()).await;
	run(std::env::args().collect()).await
}
