#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <ios>
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

    explicit Tracer(uint32_t hart_id) noexcept : hart_id_(hart_id) {}

    void frontend(
        uint64_t cycle,
        uint32_t port_id,
        FrontendStage stage,
        uint64_t uop_id,
        bool flush
    ) noexcept {
        auto &instr = instrs_in_flight_[uop_id];

        if (stage == FrontendStage::F0) {
            instr = Instr(Stage::F0, uop_id);
        }

        switch (stage) {
        case FrontendStage::F0:
            instr.stage = Stage::F0;
            break;

        case FrontendStage::F1:
            instr.stage = Stage::F1;
            break;

        case FrontendStage::F2:
            instr.stage = Stage::F2;
            instr.f3_deadline = cycle + 1;
            break;
        }

        instr.events.emplace_back(cycle, instr.stage);

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
        auto &instr = instrs_in_flight_[uop_id];

        instr.stage = Stage::F3;
        instr.pc = uop_pc;
        instr.events.emplace_back(cycle, instr.stage);

        if (flush) {
            instr.events.emplace_back(cycle + 1, Event::flush);
            instr.finished = true;
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
        auto &instr = instrs_in_flight_[uop_id];

        switch (stage) {
        case BackendStage::Rrd:
            instr.stage = Stage::Rrd;
            break;

        case BackendStage::Ex:
            instr.stage = Stage::Ex;
            break;

        case BackendStage::Mem:
            instr.stage = Stage::Mem;
            break;

        case BackendStage::Com:
            instr.stage = Stage::Com;
            break;
        }

        instr.events.emplace_back(cycle, instr.stage);

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
        uint64_t sid;

        // a retirement id.
        uint64_t rid = 0;

        // at Stage::F2, this is the cycle number after which the instruction is presumed to have
        // not been added to the fetch buffer.
        uint64_t f3_deadline;

        std::optional<uint64_t> pc;
        bool finished = false;
        bool started_printing = false;
        std::deque<Event> events;

        bool can_print() const noexcept {
            return finished || stage >= Stage::F3;
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
        uint64_t cutoff = current_cycle_;

        for (const auto &[_, instr] : instrs_in_flight_) {
            if (!instr.finished) {
                // the instruction's event queue may yet grow.
                cutoff = std::min(cutoff, first_unprinted_cycle(instr));
            }
        }

        return cutoff;
    }

    template<class... Args>
    void print_cmd(std::string_view cmd, Args &&...args) const {
        output << cmd;
        ((output << '\t' << args), ...);
        output << '\n';
    }

    void print_cmd_i(const Instr &instr) const {
        print_cmd("I", instr.sid, instr.id, hart_id_);
    }

    void print_cmd_l(const Instr &instr, bool hover_only, std::string_view msg) const {
        print_cmd("L", instr.sid, int(hover_only), msg);
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

    void print_cmd_c(uint64_t n) const {
        print_cmd("C", n);
    }

    void set_print_cycle(uint64_t to) {
        assert(to >= last_printed_cycle_);

        if (to > 0) {
            print_cmd_c(to - last_printed_cycle_);
        }

        last_printed_cycle_ = to;
    }

    void print_cmd_s(const Instr &instr, Stage stage) const {
        print_cmd("S", instr.sid, 0, stage_name(stage));
    }

    void print_cmd_r(const Instr &instr, bool flush) const {
        print_cmd("R", instr.sid, instr.rid, int(flush));
    }

    void print_event(Stage stage, const Event &event, const Instr &instr) const {
        print_cmd_s(instr, stage);
    }

    void print_event(Event::Retire, const Event &event, const Instr &instr) const {
        print_cmd_r(instr, false);
    }

    void print_event(Event::Flush, const Event &event, const Instr &instr) const {
        print_cmd_r(instr, true);
    }

    void update(uint64_t cycle) {
        assert(cycle >= current_cycle_);
        bool progressed = cycle > current_cycle_;
        current_cycle_ = cycle;

        if (progressed) {
            // remove instructions that didn't make it to the fetch buffer.
            for (auto it = instrs_in_flight_.begin(); it != instrs_in_flight_.end();) {
                if (missed_fetch_buffer(it->second)) {
                    it = instrs_in_flight_.erase(it);
                } else {
                    ++it;
                }
            }
        }

        print_instrs();
    }

    void print_cmd_info(const Instr &instr) const {
        if (!instr.pc) {
            return;
        }

        std::ostringstream msg;
        // TODO: show disasm.
        msg << "pc = " << std::hex << *instr.pc;
        print_cmd_l(instr, false, std::move(msg).str());
    }

    void print_instrs() {
        using Elem = std::pair<Event, Instr &>;

        uint64_t cutoff = find_cutoff();
        auto cmp = [&](const Elem &lhs, const Elem &rhs) {
            return lhs.first.cycle > rhs.first.cycle;
        };
        std::priority_queue<Elem, std::vector<Elem>, decltype(cmp)> events(cmp);

        std::vector<uint64_t> finished_instrs;

        for (auto &[id, instr] : instrs_in_flight_) {
            if (first_unprinted_cycle(instr) > cutoff) {
                break;
            }

            if (!instr.can_print()) {
                continue;
            }

            while (!instr.events.empty()) {
                auto &event = instr.events.front();

                if (event.cycle > cutoff) {
                    break;
                }

                events.emplace(event, instr);
                instr.events.pop_front();
            }

            if (instr.events.empty() && instr.finished) {
                finished_instrs.push_back(id);
            }
        }

        for (; !events.empty(); events.pop()) {
            const auto &event = events.top().first;
            auto &instr = events.top().second;
            set_print_cycle(event.cycle);

            if (!instr.started_printing) {
                instr.started_printing = true;
                print_cmd_i(instr);
                print_cmd_info(instr);
            }

            std::visit([&](auto &&kind) { print_event(kind, event, instr); }, event.kind);
        }

        for (auto id : finished_instrs) {
            instrs_in_flight_.erase(id);
        }
    }

    uint32_t hart_id_;
    uint64_t current_cycle_ = 0;
    uint64_t last_printed_cycle_ = 0;
    uint64_t next_sid_ = 0;
    uint64_t next_rid_ = 0;

    // instructions not yet written to the log. instructions with lower ids start earlier.
    std::map<uint64_t, Instr> instrs_in_flight_;

    static std::unordered_map<uint32_t, Tracer> tracers;
    static std::ofstream output;
};

std::filesystem::path kanata_log_path() {
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    if (const auto *path = getenv("KANATA_LOG_PATH")) {
        return path;
    }

    return "kanata.log";
}

std::unordered_map<uint32_t, Tracer> Tracer::tracers;
std::ofstream Tracer::output(kanata_log_path());

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
