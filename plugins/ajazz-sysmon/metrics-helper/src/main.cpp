// btop-metrics-helper (Phase 0, Linux) — reuse btop's collectors and emit the
// unified metrics JSON schema, one object per line on stdout.
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
    std::printf("\"net\":{\"down_bytes_s\":%llu,\"up_bytes_s\":%llu}}\n",
                static_cast<unsigned long long>(net_down),
                static_cast<unsigned long long>(net_up));
    std::fflush(stdout);
}

int main(int argc, char** argv) {
    const bool once = argc > 1 && string(argv[1]) == "--once";

    Shared::init(); // mandatory; probes hardware and primes Cpu/Mem deltas

    if (once) {
        // One real delta window so the %-based fields are meaningful.
        Cpu::collect(false);
        Net::collect(false);
        std::this_thread::sleep_for(std::chrono::milliseconds(600));
        emit_snapshot();
        return 0;
    }

    for (;;) {
        emit_snapshot();
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}
