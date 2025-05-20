import "DPI-C" function void kanata_tracer_frontend_stage(
    input longint unsigned cycle,
    input int unsigned hart_id,
    input int unsigned port_id,
    input int unsigned stage_id,
    input longint unsigned uop_id,
    input bit flush
);

import "DPI-C" function void kanata_tracer_fetch_buffer(
    input longint unsigned cycle,
    input int unsigned hart_id,
    input int unsigned port_id,
    input longint unsigned uop_id,
    input longint unsigned uop_pc,
    input bit flush
);

import "DPI-C" function void kanata_tracer_scalar_backend_stage(
    input longint unsigned cycle,
    input int unsigned hart_id,
    input int unsigned port_id,
    input int unsigned stage_id,
    input longint unsigned uop_id,
    input bit flush
);

module KanataTracerCycleCounter (
    input logic clock,
    input logic reset,
    output longint unsigned cycle
);

    always @(posedge clock) begin
        if (reset) begin
            cycle <= 'd0;
        end else begin
            cycle <= cycle + 1;
        end
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

    longint unsigned cycle;
    KanataTracerCycleCounter ctr (
        .clock(clock),
        .reset(reset),
        .cycle(cycle)
    );

    always @(posedge clock) begin
        if (!reset && uopId_valid) begin
            kanata_tracer_frontend_stage(cycle, hartId, portId, stageId, uopId_bits, flush);
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

    longint unsigned cycle;
    KanataTracerCycleCounter ctr (
        .clock(clock),
        .reset(reset),
        .cycle(cycle)
    );

    always @(posedge block) begin
        if (!reset && uopId_valid) begin
            kanata_tracer_fetch_buffer(cycle, hartId, portId, uopId_bits, uopPc, flush);
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
    input logic flush
);

    longint unsigned cycle;
    KanataTracerCycleCounter ctr (
        .clock(clock),
        .reset(reset),
        .cycle(cycle)
    );

    always @(posedge clock) begin
        if (!reset) begin
            kanata_tracer_scalar_backend_stage(cycle, hartId, portId, stageId, uopId_bits, flush);
        end
    end

endmodule
