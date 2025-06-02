#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iostream>
#include <map>
#include <optional>
#include <queue>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace {

class Tracer {
public:
    // NOLINTNEXTLINE(performance-enum-size)
    enum class FrontendStage : uint32_t {
        F0,
        F1,
        F2,
    };

    // NOLINTNEXTLINE(performance-enum-size)
    enum class BackendStage : uint32_t {
        Rrd,
        Ex,
        Mem,
        Com,
    };

    explicit Tracer(uint32_t hart_id) noexcept : hart_id_(hart_id) {
        static bool _initialized = [&] {
            read_disasm();
            return true;
        }();
        output_ << "Kanata\t0004\n";
    }

    void frontend(
        uint64_t cycle,
        uint32_t port_id,
        FrontendStage stage,
        uint64_t uop_id,
        bool flush
    ) noexcept {
        if (debug) {
            std::cerr << "frontend(cycle = " << cycle << ", port_id = " << port_id
                      << ", stage = " << int(stage) << ", uop_id = " << uop_id
                      << ", flush = " << flush << ")\n";
        }

        auto &instr = instrs_in_flight_[uop_id];

        switch (stage) {
        case FrontendStage::F0:
            instr = Instr(Stage::F0, uop_id);
            instr.set_stage(cycle, Stage::F0, true);
            break;

        case FrontendStage::F1:
            instr.set_stage(cycle, Stage::F1);
            break;

        case FrontendStage::F2:
            instr.set_stage(cycle, Stage::F2);
            instr.f3_deadline = cycle + 1;
            break;
        }

        if (flush) {
            instr.events.emplace_back(cycle + 1, Event::flush);
            instr.finished = true;
        }

        update(cycle);
    }

    void fetch_buffer(
        uint64_t cycle,
        uint32_t port_id,
        uint64_t uop_id,
        uint64_t uop_pc,
        bool flush
    ) noexcept {
        if (debug) {
            std::cerr << "fetch_buffer(cycle = " << cycle << ", port_id = " << port_id
                      << ", uop_id = " << uop_id << ", uop_pc = " << uop_pc << ", flush = " << flush
                      << ")\n";
        }

        auto &instr = instrs_in_flight_.at(uop_id);
        instr.pc = uop_pc;

        instr.set_stage(cycle, Stage::F3);

        if (flush) {
            instr.f3_flushed = {cycle + 1, {cycle + 1, Event::flush}};
        }

        update(cycle);
    }

    void backend(
        uint64_t cycle,
        uint32_t port_id,
        BackendStage stage,
        uint64_t uop_id,
        bool flush
    ) noexcept {
        if (debug) {
            std::cerr << "backend(cycle = " << cycle << ", port_id = " << port_id
                      << ", stage = " << int(stage) << ", uop_id = " << uop_id
                      << ", flush = " << flush << ")\n";
        }

        auto &instr = instrs_in_flight_.at(uop_id);

        switch (stage) {
        case BackendStage::Rrd:
            instr.set_stage(cycle, Stage::Rrd);
            break;

        case BackendStage::Ex:
            instr.set_stage(cycle, Stage::Ex);
            break;

        case BackendStage::Mem:
            instr.set_stage(cycle, Stage::Mem);
            break;

        case BackendStage::Com:
            instr.set_stage(cycle, Stage::Com);
            break;
        }

        if (flush) {
            instr.events.emplace_back(cycle + 1, Event::flush);
            instr.finished = true;
        } else if (instr.stage == Stage::Com) {
            instr.events.emplace_back(cycle + 1, Event::retire);
            // TODO: Shuttle may have its own rid?
            instr.rid = next_rid_++;
            instr.finished = true;
        }

        update(cycle);
    }

    static Tracer &get_for(uint32_t hart_id) noexcept {
        if (auto it = tracers.find(hart_id); it != tracers.end()) {
            return it->second;
        }

        auto [it, _] = tracers.insert({hart_id, Tracer(hart_id)});

        return it->second;
    }

    static void finish() {
        for (auto &[_, tracer] : tracers) {
            tracer.print_instrs(true);
        }

        tracers.clear();
    }

private:
    enum class Stage : uint8_t {
        F0,
        F1,
        F2,
        F3,
        Rrd,
        Ex,
        Mem,
        Com,
        // TODO: WB. there are some weird shenanigans going on with that.
    };

    struct Event {
        // clang-format off: looks prettier as one-liner.
        static constexpr struct Retire {} retire {};
        static constexpr struct Flush {} flush {};
        // clang-format on

        using Kind = std::variant<Stage, Retire, Flush>;

        Event(uint64_t cycle, Kind kind) : cycle(cycle), kind(kind) {}

        uint64_t cycle;
        Kind kind;
    };

    struct Instr {
        Instr() = default;

        Instr(Stage stage, uint64_t id) : stage(stage), id(id) {}

        Stage stage;

        // an instruction id assigned at the F0 stage.
        uint64_t id;

        // a serial instruction id in the log.
        std::optional<uint64_t> sid;

        // a retirement id.
        uint64_t rid = 0;

        // at Stage::F2, this is the cycle number after which the instruction is presumed to have
        // not been added to the fetch buffer.
        uint64_t f3_deadline;

        // if flushed as Stage::F3, the cycle number after which we record the flush event.
        // (the instruction may also land into the backend pipeline,
        // in which case we ignore the flushing.)
        std::optional<std::pair<uint64_t, Event>> f3_flushed;

        uint64_t entered_rrd_at;

        std::optional<uint64_t> pc;
        bool finished = false;
        bool started_printing = false;
        std::deque<Event> events;

        bool can_print(uint64_t cycle) const noexcept {
            return finished || stage >= Stage::Rrd && entered_rrd_at + 1 <= cycle;
        }

        void set_stage(uint64_t cycle, Stage stage, bool force = false) {
            bool rrd_to_f3 = this->stage == Stage::Rrd && stage == Stage::F3;

            if (force || this->stage != stage && !rrd_to_f3) {
                if (this->stage < Stage::Rrd && stage >= Stage::Rrd) {
                    entered_rrd_at = cycle;
                }

                this->stage = stage;

                if (!events.empty() && events.back().cycle == cycle &&
                    std::holds_alternative<Stage>(events.back().kind)) {

                    events.back().kind = stage;
                } else {
                    events.emplace_back(cycle, stage);
                }
            }
        }
    };

    uint64_t first_unprinted_cycle(const Instr &instr) const noexcept {
        if (!instr.events.empty()) {
            return instr.events.front().cycle;
        }

        return current_cycle_;
    }

    bool missed_fetch_buffer(const Instr &instr) const noexcept {
        return !instr.finished && instr.stage == Stage::F2 && current_cycle_ > instr.f3_deadline;
    }

    uint64_t find_cutoff() const noexcept {
        uint64_t cutoff = current_cycle_ == 0 ? 0 : current_cycle_ - 1;
        uint64_t id = -1;

        for (const auto &[_, instr] : instrs_in_flight_) {
            if (!instr.finished) {
                if (first_unprinted_cycle(instr) < cutoff) {
                    id = instr.id;
                }

                // the instruction's event queue may yet grow.
                cutoff = std::min(cutoff, first_unprinted_cycle(instr));
            }
        }

        if (debug) {
            std::cerr << "    cutoff: " << cutoff << " because of instr " << id << "\n";
        }

        return cutoff;
    }

    template<class... Args>
    void print_cmd(std::string_view cmd, Args &&...args) {
        output_ << cmd;
        ((output_ << '\t' << args), ...);
        output_ << '\n';
    }

    void print_cmd_i(const Instr &instr) {
        print_cmd("I", *instr.sid, instr.id, hart_id_);
    }

    void print_cmd_l(const Instr &instr, bool hover_only, std::string_view msg) {
        print_cmd("L", *instr.sid, int(hover_only), msg);
    }

    static std::string_view stage_name(Stage stage) noexcept {
        switch (stage) {
        case Stage::F0:
            return "F0";

        case Stage::F1:
            return "F1";

        case Stage::F2:
            return "F2";

        case Stage::F3:
            return "F3";

        case Stage::Rrd:
            return "RRD";

        case Stage::Ex:
            return "EX";

        case Stage::Mem:
            return "MEM";

        case Stage::Com:
            return "COM";
        }

        return "<idk>";
    }

    void print_cmd_c(uint64_t n) {
        print_cmd("C", n);
    }

    void set_print_cycle(uint64_t to) {
        assert(to >= last_printed_cycle_);

        if (to > last_printed_cycle_) {
            print_cmd_c(to - last_printed_cycle_);
        }

        last_printed_cycle_ = to;
    }

    void print_cmd_s(const Instr &instr, Stage stage) {
        print_cmd("S", *instr.sid, 0, stage_name(stage));
    }

    void print_cmd_r(const Instr &instr, bool flush) {
        print_cmd("R", *instr.sid, instr.rid, int(flush));
    }

    void print_event(Stage stage, const Event &event, const Instr &instr) {
        print_cmd_s(instr, stage);
    }

    void print_event(Event::Retire, const Event &event, const Instr &instr) {
        print_cmd_r(instr, false);
    }

    void print_event(Event::Flush, const Event &event, const Instr &instr) {
        print_cmd_r(instr, true);
    }

    void update(uint64_t cycle) {
        assert(cycle >= current_cycle_);
        bool progressed = cycle > current_cycle_;
        current_cycle_ = cycle;

        if (progressed) {
            // remove instructions that didn't make it into the fetch buffer.
            for (auto it = instrs_in_flight_.begin(); it != instrs_in_flight_.end();) {
                if (missed_fetch_buffer(it->second)) {
                    if (debug) {
                        std::cerr << "    removing " << it->first
                                  << ": didn't make it into the fetch buffer\n";
                    }

                    it = instrs_in_flight_.erase(it);
                } else {
                    ++it;
                }
            }

            // record flush events for instructions past their f3_flushed deadline.
            for (auto &[id, instr] : instrs_in_flight_) {
                if (instr.f3_flushed && instr.stage != Stage::F3) {
                    if (debug) {
                        std::cerr << "    disarming flush event at F3 for instruction " << id
                                  << " (at " << instr.f3_flushed->first << ") in stage "
                                  << stage_name(instr.stage) << '\n';
                    }

                    instr.f3_flushed = {};

                    continue;
                }

                if (instr.f3_flushed && instr.f3_flushed->first >= current_cycle_) {

                    if (debug) {
                        std::cerr << "    recording flush event at F3 for instruction " << id
                                  << " (at " << instr.f3_flushed->first << ")\n";
                    }

                    instr.events.push_back(instr.f3_flushed->second);
                    instr.f3_flushed = {};
                    instr.finished = true;
                }
            }
        }

        print_instrs();

        if (debug) {
            std::cerr << "  update(" << cycle << "): " << instrs_in_flight_.size()
                      << " instrs in flight remain\n";
        }
    }

    void print_cmd_info(const Instr &instr) {
        if (!instr.pc) {
            return;
        }

        std::ostringstream msg;
        msg << std::hex << *instr.pc;

        if (auto it = disasm.find(*instr.pc); it != disasm.end()) {
            msg << ": " << it->second;
        }

        print_cmd_l(instr, false, std::move(msg).str());
    }

    void print_instrs(bool force = false) {
        using Elem = std::pair<Event, Instr *>;

        if (current_cycle_ == 0) {
            return;
        }

        uint64_t cutoff = find_cutoff();

        auto cmp = [&](const Elem &lhs, const Elem &rhs) {
            return lhs.first.cycle > rhs.first.cycle ||
                   (lhs.first.cycle == rhs.first.cycle && lhs.second->id > rhs.second->id);
        };
        std::priority_queue<Elem, std::vector<Elem>, decltype(cmp)> events(cmp);

        std::vector<uint64_t> finished_instrs;

        for (auto it = instrs_in_flight_.begin(); it != instrs_in_flight_.end(); ++it) {
            auto id = it->first;
            auto &instr = it->second;

            if (debug) {
                std::cerr << "  - instruction " << id << ": FUC = " << first_unprinted_cycle(instr)
                          << ", can_print() = " << instr.can_print(current_cycle_) << " (stage "
                          << stage_name(instr.stage) << ", event count " << instr.events.size()
                          << "), finished = " << instr.finished
                          << ", started_printing = " << instr.started_printing
                          << "; events.size() = " << events.size() << "\n"
                          << std::flush;
            }

            if (!instr.can_print(current_cycle_)) {
                continue;
            }

            while (!instr.events.empty()) {
                auto &event = instr.events.front();

                if (event.cycle > cutoff || force) {
                    break;
                }

                events.emplace(event, &instr);
                instr.events.pop_front();
            }

            if (instr.events.empty() && instr.finished) {
                finished_instrs.push_back(id);
            }
        }

        if (debug) {
            std::cerr << "  processed event count: " << events.size() << "\n" << std::flush;
        }

        for (; !events.empty(); events.pop()) {
            const auto &event = events.top().first;
            auto &instr = *events.top().second;
            set_print_cycle(event.cycle);

            if (!instr.started_printing) {
                instr.started_printing = true;
                instr.sid = next_sid_++;

                if (debug) {
                    std::cerr << "  > started instruction " << instr.id << " (sid " << *instr.sid
                              << ")\n";
                }

                print_cmd_i(instr);
                print_cmd_info(instr);
            }

            std::visit([&](auto &&kind) { print_event(kind, event, instr); }, event.kind);
        }

        if (debug) {
            std::cerr << "  removing finished instructions (count = " << finished_instrs.size()
                      << ")\n"
                      << std::flush;
        }

        for (auto id : finished_instrs) {
            instrs_in_flight_.erase(id);
        }
    }

    std::filesystem::path kanata_log_path() const {
        std::string base_path;

        // NOLINTNEXTLINE(concurrency-mt-unsafe)
        if (const auto *path = getenv("KANATA_LOG_PATH")) {
            base_path = path;
        } else {
            base_path = "kanata.log";
        }

        if (base_path.size() > 4 && base_path.substr(base_path.size() - 4) == ".log") {
            base_path.erase(
                base_path.begin() + std::string::difference_type(base_path.size() - 4),
                base_path.end()
            );
        }

        std::ostringstream path;
        path << base_path << ".t" << hart_id_ << ".log";

        std::cerr << "writing the kanata log to " << path.str() << "\n";

        return std::move(path).str();
    }

    void read_disasm() {
        {
            std::ifstream f("./disasm.d");

            if (!f) {
                return;
            }

            std::ostringstream buf;
            buf << f.rdbuf();

            disasm_contents = std::move(buf).str();
        }

        for (std::string::size_type start = 0, end = 0;
             (end = disasm_contents.find('\n', start)) != std::string::npos;
             start = end + 1) {
            auto line = disasm_contents.substr(start, end - start);

            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }

            unsigned long long addr = 0;
            size_t disasm_start = 0;
            auto r = std::sscanf(line.data(), "%llx: %*x %zn", &addr, &disasm_start);

            if (r != 1) {
                std::cerr << "scanf returned " << r << '\n';
                std::cerr << "  line: " << line << '\n';
                continue;
            }

            if (disasm_start >= line.size()) {
                std::cerr << "disasm_start = " << disasm_start
                          << " >= line.size() = " << line.size() << '\n';
                std::cerr << "  line: " << line << '\n';
                continue;
            }

            auto d = line.substr(disasm_start);

            for (char &c : d) {
                if (c == '\t') {
                    c = ' ';
                }
            }

            disasm.insert({addr, std::move(d)});
        }
    }

    uint32_t hart_id_;
    uint64_t current_cycle_ = 0;
    uint64_t last_printed_cycle_ = 0;
    uint64_t next_sid_ = 0;
    uint64_t next_rid_ = 0;

    // instructions not yet written to the log. instructions with lower ids start earlier.
    std::map<uint64_t, Instr> instrs_in_flight_;

    std::ofstream output_{kanata_log_path()};

    static bool debug;
    static std::unordered_map<uint32_t, Tracer> tracers;
    static std::string disasm_contents;
    static std::unordered_map<uint64_t, std::string> disasm;
};

bool is_debug_enabled() noexcept {
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    auto *env = getenv("KANATA_DEBUG");

    return env != nullptr && *env != '\0' && strcmp(env, "0") != 0;
}

bool Tracer::debug = is_debug_enabled();
std::unordered_map<uint32_t, Tracer> Tracer::tracers;
std::string Tracer::disasm_contents;
std::unordered_map<uint64_t, std::string> Tracer::disasm;

} // namespace

extern "C" void kanata_tracer_frontend_stage(
    uint64_t cycle,
    uint32_t hart_id,
    uint32_t port_id,
    uint32_t stage_id,
    uint64_t uop_id,
    bool flush
) {
    Tracer::get_for(hart_id).frontend(
        cycle, port_id, static_cast<Tracer::FrontendStage>(stage_id), uop_id, flush
    );
}

extern "C" void kanata_tracer_fetch_buffer(
    uint64_t cycle,
    uint32_t hart_id,
    uint32_t port_id,
    uint64_t uop_id,
    uint64_t uop_pc,
    bool flush
) {
    Tracer::get_for(hart_id).fetch_buffer(cycle, port_id, uop_id, uop_pc, flush);
}

extern "C" void kanata_tracer_scalar_backend_stage(
    uint64_t cycle,
    uint32_t hart_id,
    uint32_t port_id,
    uint32_t stage_id,
    uint64_t uop_id,
    bool flush
) {
    Tracer::get_for(hart_id).backend(
        cycle, port_id, static_cast<Tracer::BackendStage>(stage_id), uop_id, flush
    );
}

extern "C" void kanata_tracer_finish() {
    Tracer::finish();
}
