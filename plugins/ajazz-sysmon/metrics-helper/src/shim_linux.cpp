// Link shim for the btop-metrics-helper (Linux).
//
// We compile btop's collector translation units (btop_collect.cpp, btop_shared,
// btop_config, btop_tools, btop_log) but NOT its TUI (btop.cpp, btop_draw.cpp,
// btop_menu/input). Those TUI units define a handful of globals the collectors
// still reference; we provide inert definitions here so the subset links.
//
// One of these is load-bearing, not inert: `<Box>::width` bounds each history
// deque at `width * 2` samples (btop_collect.cpp:1185 and siblings). A width of
// 0 pops every freshly-collected value straight back off, leaving empty deques
// and a constant 0% reading. Set a real history length.
#include "btop_shared.hpp"
#include "btop_tools.hpp"

#include <atomic>
#include <cstdlib>
#include <string>

#include <unistd.h>

using std::string;

// --- Global:: (normally in btop.cpp) ---
namespace Global {
const string Version = "0.0.0-helper";
string exit_error_msg;
std::atomic<bool> resized{false};
std::atomic<bool> init_conf{false};
uid_t real_uid = getuid(), set_uid = getuid();
} // namespace Global

// --- Runner:: (normally in btop.cpp) ---
namespace Runner {
std::atomic<bool> stopping{false};
std::atomic<bool> coreNum_reset{false};
Tools::atomic_waiting_lock active;
} // namespace Runner

// --- global clean_quit (normally in btop.cpp) ---
void clean_quit(int sig) {
    std::exit(sig < 0 ? 0 : sig);
}

// --- box-dimension + UI-state globals (normally in btop_draw.cpp) ---
// `width` is load-bearing (history-deque cap); the rest are inert.
static constexpr int kHistoryWidth = 100; // -> up to 200 retained samples
namespace Cpu {
int width = kHistoryWidth, min_width = 0, min_height = 0;
}
namespace Mem {
int width = kHistoryWidth, min_width = 0, min_height = 0;
bool redraw = false;
} // namespace Mem
namespace Net {
int width = kHistoryWidth, min_height = 0;
bool redraw = false;
} // namespace Net
namespace Proc {
int width = kHistoryWidth, min_width = 0, min_height = 0;
bool shown = false, redraw = false;
int select_max = 0;
int selected_pid = 0, start = 0, selected = 0, selected_depth = 0;
string selected_name;
} // namespace Proc

// --- Fx::reset (terminal reset escape, normally in btop.cpp) ---
namespace Fx {
string reset = "\033[0m";
}
