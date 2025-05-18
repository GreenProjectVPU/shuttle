#include <cstdint>
#include <unordered_map>

namespace {

class Tracer {
public:
    struct Params {
        uint32_t hart_id;
        size_t fetch_width;
        size_t retire_width;
        size_t fetch_buffer_size;
    };

    enum class FrontendStage: uint32_t {
        F0,
        F1,
        F2,
    };

    enum class BackendStage: uint32_t {
        Rrd,
        Ex,
        Mem,
        Com,
    };

    explicit Tracer(Params params) noexcept : params_(params) {}

    void frontend(uint32_t port_id, FrontendStage stage, uint64_t uop_id, bool flush) noexcept {
        // TODO
    }

    void fetch_buffer(uint32_t port_id, uint64_t uop_id, uint64_t uop_pc, bool flush) noexcept {
        // TODO
    }

    void backend(uint32_t port_id, BackendStage stage, uint64_t uop_id, bool wb_pending, bool flush) noexcept {
        // TODO
    }

private:
    Params params_;
};

std::unordered_map<uint32_t, Tracer> tracers;

} // namespace

extern "C" void kanata_tracer_params(
    uint32_t hart_id,
    uint32_t fetch_width,
    uint32_t retire_width,
    uint32_t fetch_buffer_size
) {
    tracers.insert({hart_id, Tracer({
        hart_id,
        fetch_width,
        retire_width,
        fetch_buffer_size,
    })});
}

extern "C" void kanata_tracer_frontend_stage(
    uint32_t hart_id,
    uint32_t port_id,
    uint32_t stage_id,
    uint64_t uop_id,
    bool flush
) {
    tracers[hart_id].frontend(
        port_id,
        static_cast<Tracer::FrontendStage>(stage_id),
        uop_id,
        flush
    );
}

extern "C" void kanata_tracer_fetch_buffer(
    uint32_t hart_id,
    uint32_t port_id,
    uint64_t uop_id,
    uint64_t uop_pc,
    bool flush
) {
    tracers[hart_id].fetch_buffer(port_id, uop_id, uop_pc, flush);
}

extern "C" void kanata_tracer_scalar_backend_stage(
    uint32_t hart_id,
    uint32_t port_id,
    uint32_t stage_id,
    uint64_t uop_id,
    bool flush,
    bool wb_pending
) {
    tracers[hart_id].backend(
        port_id,
        static_cast<Tracer::BackendStage>(stage_id),
        uop_id,
        wb_pending,
        flush
    );
}
