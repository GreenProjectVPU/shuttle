import "DPI-C" function void kanata_tracer_params(
    input int unsigned hartId,
    input int unsigned fetchWidth,
    input int unsigned retireWidth,
    input int unsigned fetchBufferSize
);

import "DPI-C" function void kanata_tracer_frontend_stage(
    input int unsigned hart_id,
    input int unsigned port_id,
    input int unsigned stage_id,
    input longint unsigned uop_id,
    input bit flush
);

import "DPI-C" function void kanata_tracer_fetch_buffer(
    input int unsigned hart_id,
    input int unsigned port_id,
    input longint unsigned uop_id,
    input longint unsigned uop_pc,
    input bit flush
);

import "DPI-C" function void kanata_tracer_scalar_backend_stage(
    input int unsigned hart_id,
    input int unsigned port_id,
    input int unsigned stage_id,
    input longint unsigned uop_id,
    input bit flush,
    input bit wb_pending
);

module KanataTracerParams (
    input int unsigned hartId,
    input int unsigned fetchWidth,
    input int unsigned retireWidth,
    input int unsigned fetchBufferSize
);

    initial begin
        kanata_tracer_params(hartId, fetchWidth, retireWidth, fetchBufferSize);
    end

endmodule

module FrontendStageTracer (
    input logic clock,
    input logic reset,
    input logic [31:0] hartId,
    input logic [31:0] portId,
    input logic [1:0] stageId,
    input logic uopId_valid,
    input logic [63:0] uopId_bits,
    input logic flush
);

    always @(posedge clock) begin
        if (!reset && uopId_valid) begin
            kanata_tracer_frontend_stage(hartId, portId, stageId, uopId_bits, flush);
        end
    end

endmodule

module FetchBufferTracer #(
    parameter int VADDR_BITS_EXTENDED = 40
) (
    input logic clock,
    input logic reset,
    input logic [31:0] hartId,
    input logic [31:0] portId,
    input logic uopId_valid,
    input logic [63:0] uopId_bits,
    input logic [VADDR_BITS_EXTENDED - 1 : 0] uopPc,
    input logic flush
);

    always @(posedge block) begin
        if (!reset && uopId_valid) begin
            kanata_tracer_fetch_buffer(hartId, portId, uopId_bits, uopPc, flush);
        end
    end

endmodule

module ScalarBackendStageTracer (
    input logic clock,
    input logic reset,
    input logic [31:0] hartId,
    input logic [31:0] portId,
    input logic [2:0] stageId,
    input logic uopId_valid,
    input logic [63:0] uopId_bits,
    input logic flush,
    input logic wbPending
);

    always @(posedge clock) begin
        if (!reset) begin
            kanata_tracer_scalar_backend_stage(hartId, portId, stageId, uopId_bits, flush,
                                               wbPending);
        end
    end

endmodule
