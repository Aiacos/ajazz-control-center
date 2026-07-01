// btop-metrics-helper (Linux) — reuse btop's collectors and emit the unified
// metrics JSON schema, one object per line on stdout. CPU/mem/net since
// Phase 0; GPU (GPU_SUPPORT: nvml/rocm-smi/amdgpu-sysfs/intel) since Phase 3.
//
//   --once    emit a single primed snapshot and exit
//   (default) stream one line per second
//
// The point of this helper is "read directly from btop": it links btop's real
// collector translation units (see CMakeLists.txt) plus a small shim
// (shim_linux.cpp) that provides the TUI/box globals btop.cpp / btop_draw.cpp
// would otherwise define. No sensor logic is reimplemented here.
#include "btop_shared.hpp"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <string>
#include <thread>
#include <unordered_map>

using std::string;

static long long back(const std::deque<long long>& d) {
    return d.empty() ? 0 : d.back();
}

static std::uint64_t stat_u(const std::unordered_map<string, std::uint64_t>& m, const string& k) {
    const auto it = m.find(k);
    return it == m.end() ? 0 : it->second;
}

#ifdef GPU_SUPPORT
static const char* json_bool(bool v) {
    return v ? "true" : "false";
}

// Minimal JSON string escaping for device names: backslash + double quote,
// and drop control characters (they would break the hand-rolled printf JSON).
static string json_escape(const string& s) {
    string out;
    out.reserve(s.size());
    for (const char c : s) {
        if (static_cast<unsigned char>(c) < 0x20)
            continue;
        if (c == '\\' || c == '"')
            out += '\\';
        out += c;
    }
    return out;
}
#endif

static void emit_snapshot() {
    const Cpu::cpu_info& cpu = Cpu::collect(false);
    const Mem::mem_info& mem = Mem::collect(false);
    const Net::net_info& net = Net::collect(false);

    const long long cpu_pct = back(cpu.cpu_percent.at("total"));
    const long long cpu_temp = cpu.temp.empty() ? 0 : back(cpu.temp.at(0));
    const std::uint64_t mem_used = stat_u(mem.stats, "used");
    const std::uint64_t mem_total = mem_used + stat_u(mem.stats, "available");
    const long long mem_pct = back(mem.percent.at("used"));
    const std::uint64_t net_down = net.stat.count("download") ? net.stat.at("download").speed : 0;
    const std::uint64_t net_up = net.stat.count("upload") ? net.stat.at("upload").speed : 0;

    std::printf("{\"schema\":1,\"backend\":\"btop-linux\",");
    std::printf("\"cpu\":{\"percent\":%lld,\"per_core\":[", cpu_pct);
    for (size_t i = 0; i < cpu.core_percent.size(); ++i)
        std::printf("%s%lld", i ? "," : "", back(cpu.core_percent[i]));
    std::printf("],\"temp_c\":%lld},", cpu_temp);
    std::printf("\"mem\":{\"used_bytes\":%llu,\"total_bytes\":%llu,\"percent\":%lld},",
                static_cast<unsigned long long>(mem_used),
                static_cast<unsigned long long>(mem_total),
                mem_pct);
    std::printf("\"net\":{\"down_bytes_s\":%llu,\"up_bytes_s\":%llu},",
                static_cast<unsigned long long>(net_down),
                static_cast<unsigned long long>(net_up));
    // "gpu" is always present (empty array when GPU_SUPPORT is off or no devices)
    // so consumers get a stable schema.
    std::printf("\"gpu\":[");
#ifdef GPU_SUPPORT
    const auto& gpus = Gpu::collect(false);
    for (size_t i = 0; i < gpus.size(); ++i) {
        const Gpu::gpu_info& g = gpus[i];
        const auto& sf = g.supported_functions;
        const string name = json_escape(i < Gpu::gpu_names.size() ? Gpu::gpu_names[i] : "");
        std::printf("%s{\"name\":\"%s\",\"util_percent\":%lld,\"temp_c\":%lld,"
                    "\"vram_used_bytes\":%lld,\"vram_total_bytes\":%lld,"
                    "\"power_w\":%.1f,\"core_clock_mhz\":%u,\"mem_clock_mhz\":%lld,"
                    "\"supported\":{\"util\":%s,\"temp\":%s,\"vram\":%s,"
                    "\"power\":%s,\"core_clock\":%s,\"mem_clock\":%s}}",
                    i ? "," : "",
                    name.c_str(),
                    back(g.gpu_percent.at("gpu-totals")),
                    back(g.temp),
                    g.mem_used,
                    g.mem_total,
                    static_cast<double>(g.pwr_usage) / 1000.0,
                    g.gpu_clock_speed,
                    g.mem_clock_speed,
                    json_bool(sf.gpu_utilization),
                    json_bool(sf.temp_info),
                    json_bool(sf.mem_total && sf.mem_used),
                    json_bool(sf.pwr_usage),
                    json_bool(sf.gpu_clock),
                    json_bool(sf.mem_clock));
    }
#endif
    std::printf("]}\n");
    std::fflush(stdout);
}

int main(int argc, char** argv) {
    const bool once = argc > 1 && string(argv[1]) == "--once";

    Shared::init(); // mandatory; probes hardware and primes Cpu/Mem deltas

    if (once) {
        // One real delta window so the %-based fields are meaningful.
        Cpu::collect(false);
        Net::collect(false);
#ifdef GPU_SUPPORT
        Gpu::collect(false);
#endif
        std::this_thread::sleep_for(std::chrono::milliseconds(600));
        emit_snapshot();
        return 0;
    }

    for (;;) {
        emit_snapshot();
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}
