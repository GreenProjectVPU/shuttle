typedef logic [63:0] isn_id_t;

class KanataLogWriter;
    int fd;

    function new();
        fd = $fopen("kanata.log", "w");

        if (fd == 0) begin
            $warning({"could not open kanata.log for writing. ",
                      "pipeline trace will not be generated."});
        end
    endfunction

    function void close();
        $fclose(fd);
    endfunction

    function void write_record(string fields[]);
        string line;

        if (fd == 0) return;

        line = fields[0];

        for (int i = 1; i < fields.size(); ++i) begin
            line = {line, "\t", fields[i]};
        end

        $fwrite(fd, {line, "\n"});
    endfunction

    function void start_record(string kind);
        if (fd == 0) return;

        $fwrite(fd, "%s", kind);
    endfunction

    function void param_int(integer value);
        if (fd == 0) return;

        $fwrite(fd, "\t%0d", value);
    endfunction

    function void param_long(longint value);
        if (fd == 0) return;

        $fwrite(fd, "\t%0d", value);
    endfunction

    function void param_bit(bit value);
        if (fd == 0) return;

        $fwrite(fd, "\t%0d", value);
    endfunction

    function void param_str(string value);
        if (fd == 0) return;

        $fwrite(fd, "\t%s", value);
    endfunction

    function void end_record();
        if (fd == 0) return;

        $fwrite(fd, "\n");
    endfunction

    function void write_header();
        write_record('{"Kanata", "0004"});
    endfunction

    function void write_cmd_c(integer cycles = 1);
        start_record("C");
        param_int(cycles);
        end_record();
    endfunction

    function void write_cmd_i(isn_id_t isn_id, longint hartid);
        start_record("I");
        param_long(isn_id);
        param_long(isn_id);
        param_long(hartid);
        end_record();
    endfunction

    function void write_cmd_l(isn_id_t isn_id, bit hover_text, string msg);
        start_record("L");
        param_long(isn_id);
        param_bit(hover_text);
        param_str(msg);
        end_record();
    endfunction

    function void write_cmd_s(isn_id_t isn_id, string stage, integer lane = 0);
        start_record("S");
        param_long(isn_id);
        param_int(lane);
        param_str(stage);
        end_record();
    endfunction

    function void write_cmd_r(isn_id_t isn_id, longint retire_id, bit flushed);
        start_record("R");
        param_long(isn_id);
        param_long(retire_id);
        param_bit(flushed);
        end_record();
    endfunction

endclass

module KanataTracer #(
    parameter integer FETCH_WIDTH,
    parameter integer RETIRE_WIDTH,
    parameter integer VADDR_BITS_EXTENDED,
    parameter integer HART_ID_BITS
) (
    input logic clock,
    input logic reset,

    input logic [HART_ID_BITS-1:0] hartid,

    input logic s0id_valid,
    input logic [63:0] s0id_bits,

    input logic s1id_valid,
    input logic [63:0] s1id_bits,

    input logic s2ids_0_valid,
    input logic s2ids_1_valid,
    input logic s2ids_2_valid,
    input logic s2ids_3_valid,
    input logic [63:0] s2ids_0_bits,
    input logic [63:0] s2ids_1_bits,
    input logic [63:0] s2ids_2_bits,
    input logic [63:0] s2ids_3_bits,
    input logic [VADDR_BITS_EXTENDED-1:0] s2pcs_0,
    input logic [VADDR_BITS_EXTENDED-1:0] s2pcs_1,
    input logic [VADDR_BITS_EXTENDED-1:0] s2pcs_2,
    input logic [VADDR_BITS_EXTENDED-1:0] s2pcs_3,

    input logic rrd_0_valid,
    input logic rrd_1_valid,
    input logic [63:0] rrd_0_bits,
    input logic [63:0] rrd_1_bits,

    input logic ex_0_valid,
    input logic ex_1_valid,
    input logic [63:0] ex_0_bits,
    input logic [63:0] ex_1_bits,

    input logic mem_0_valid,
    input logic mem_1_valid,
    input logic [63:0] mem_0_bits,
    input logic [63:0] mem_1_bits,

    input logic com_0_valid,
    input logic com_1_valid,
    input logic [63:0] com_0_bits,
    input logic [63:0] com_1_bits,

    input logic wb_0_valid,
    input logic wb_1_valid,
    input logic [63:0] wb_0_bits,
    input logic [63:0] wb_1_bits
);

    logic s2ids_valid[FETCH_WIDTH];
    assign s2ids_valid = '{s2ids_0_valid, s2ids_1_valid, s2ids_2_valid, s2ids_3_valid};
    logic [63:0] s2ids_bits[FETCH_WIDTH];
    assign s2ids_bits = '{
        s2ids_0_bits,
        s2ids_1_bits,
        s2ids_2_bits,
        s2ids_3_bits
    };
    logic [VADDR_BITS_EXTENDED-1:0] s2pcs[FETCH_WIDTH];
    assign s2pcs = '{s2pcs_0, s2pcs_1, s2pcs_2, s2pcs_3};

    logic rrd_valid[RETIRE_WIDTH];
    assign rrd_valid = '{rrd_0_valid, rrd_1_valid};
    logic [63:0] rrd_bits[RETIRE_WIDTH];
    assign rrd_bits = '{rrd_0_bits, rrd_1_bits};

    logic ex_valid[RETIRE_WIDTH];
    assign ex_valid = '{ex_0_valid, ex_1_valid};
    logic [63:0] ex_bits[RETIRE_WIDTH];
    assign ex_bits = '{ex_0_bits, ex_1_bits};

    logic mem_valid[RETIRE_WIDTH];
    assign mem_valid = '{mem_0_valid, mem_1_valid};
    logic [63:0] mem_bits[RETIRE_WIDTH];
    assign mem_bits = '{mem_0_bits, mem_1_bits};

    logic com_valid[RETIRE_WIDTH];
    assign com_valid = '{com_0_valid, com_1_valid};
    logic [63:0] com_bits[RETIRE_WIDTH];
    assign com_bits = '{com_0_bits, com_1_bits};

    logic wb_valid[RETIRE_WIDTH];
    assign wb_valid = '{wb_0_valid, wb_1_valid};
    logic [63:0] wb_bits[RETIRE_WIDTH];
    assign wb_bits = '{wb_0_bits, wb_1_bits};

    string comment;

    //

    KanataLogWriter log;

    initial begin
        log = new();
        log.write_header();
    end

    final begin
        log.close();
    end

    always @(posedge clock) begin
        if (!reset) begin
            // Stage F0.
            if (s0id_valid) begin
                // Register new instructions.
                for (int i = 0; i < FETCH_WIDTH; ++i) begin
                    log.write_cmd_i(s0id_bits + 64'(i), longint'(hartid));
                end

                // Enter stage F0.
                for (int i = 0; i < FETCH_WIDTH; ++i) begin
                    log.write_cmd_s(s0id_bits + 64'(i), "F0");
                end
            end

            // Stage F1.
            if (s1id_valid) begin
                for (int i = 0; i < FETCH_WIDTH; ++i) begin
                    log.write_cmd_s(s1id_bits + 64'(i), "F1");
                end
            end

            // Stage F2.
            for (int i = 0; i < FETCH_WIDTH; ++i) begin
                if (s2ids_valid[i]) begin
                    log.write_cmd_s(s2ids_bits[i], "F2");

                    // TODO: print the disassembled instruction.
                    comment = "";
                    $swrite(comment, "PC: %0x", s2pcs[i]);
                    log.write_cmd_l(s2ids_bits[i], 0, comment);
                end
            end

            // Stage RRD.
            for (int i = 0; i < RETIRE_WIDTH; ++i) begin
                if (rrd_valid[i]) begin
                    log.write_cmd_s(rrd_bits[i], "RRD");
                end
            end

            // Stage EX.
            for (int i = 0; i < RETIRE_WIDTH; ++i) begin
                if (ex_valid[i]) begin
                    log.write_cmd_s(ex_bits[i], "EX");
                end
            end

            // Stage MEM.
            for (int i = 0; i < RETIRE_WIDTH; ++i) begin
                if (mem_valid[i]) begin
                    log.write_cmd_s(mem_bits[i], "MEM");
                end
            end

            // Stage COM.
            for (int i = 0; i < RETIRE_WIDTH; ++i) begin
                if (com_valid[i]) begin
                    log.write_cmd_s(com_bits[i], "COM");
                end
            end

            // Stage WB.
            for (int i = 0; i < RETIRE_WIDTH; ++i) begin
                if (wb_valid[i]) begin
                    log.write_cmd_s(wb_bits[i], "WB");
                end
            end

            // Clock boundary.
            log.write_cmd_c();

            // End the WB stage.
            for (int i = 0; i < RETIRE_WIDTH; ++i) begin
                if (wb_valid[i]) begin
                    log.write_cmd_r(wb_bits[i], wb_bits[i], 0);
                end
            end
        end
    end

endmodule
