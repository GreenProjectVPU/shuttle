#include <cstdint>
#include <unordered_map>

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
        // TODO
    }

    void fetch_buffer(
        uint64_t cycle,
        uint32_t port_id,
        uint64_t uop_id,
        uint64_t uop_pc,
        bool flush
    ) noexcept {
        // TODO
    }

    void backend(
        uint64_t cycle,
        uint32_t port_id,
        BackendStage stage,
        uint64_t uop_id,
        bool wb_pending,
        bool flush
    ) noexcept {
        // TODO
    }

    static Tracer &get_for(uint32_t hart_id) noexcept {
        if (auto it = tracers.find(hart_id); it != tracers.end()) {
            return it->second;
        }

        auto [it, _] = tracers.insert({hart_id, Tracer(hart_id)});

        return it->second;
    }

private:
    uint32_t hart_id_;

    static std::unordered_map<uint32_t, Tracer> tracers;
};

std::unordered_map<uint32_t, Tracer> Tracer::tracers;

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
    bool flush,
    bool wb_pending
) {
    Tracer::get_for(hart_id).backend(
        cycle, port_id, static_cast<Tracer::BackendStage>(stage_id), uop_id, wb_pending, flush
    );
}
