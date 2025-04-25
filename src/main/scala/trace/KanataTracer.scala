package shuttle.trace

import chisel3._
import chisel3.util.{HasBlackBoxResource, Valid}
import freechips.rocketchip.tile.MaxHartIdBits
import org.chipsalliance.cde.config.Parameters
import shuttle.common.ShuttleUOP

class KanataTracer(fetchWidth: Int,
                   retireWidth: Int,
                   vaddrBitsExtended: Int)(implicit p: Parameters)
  extends BlackBox(Map(
    "FETCH_WIDTH" -> fetchWidth,
    "RETIRE_WIDTH" -> retireWidth,
    "VADDR_BITS_EXTENDED" -> vaddrBitsExtended,
    "HART_ID_BITS" -> p(MaxHartIdBits),
  ))
    with HasBlackBoxResource {

  private def pipelineUopIds = Vec(retireWidth, Valid(UInt(64.W)))

  val io = IO(new Bundle {
    val clock = Input(Clock())
    val reset = Input(Bool())
    val hartid = Input(UInt(p(MaxHartIdBits).W))
    val s0id = Input(Valid(UInt(64.W)))
    val s1id = Input(Valid(UInt(64.W)))
    val s2ids = Input(Vec(fetchWidth, Valid(UInt(64.W))))
    val s2pcs = Input(Vec(fetchWidth, UInt(vaddrBitsExtended.W)))
    val rrd, ex, mem, com, wb = Input(pipelineUopIds)
  })

  addResource("/shuttle/vsrc/KanataTracer.v")
}
