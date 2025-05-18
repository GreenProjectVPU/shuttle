package shuttle.trace

import chisel3._
import chisel3.util.{HasBlackBoxResource, Valid}
import freechips.rocketchip.tile.MaxHartIdBits
import org.chipsalliance.cde.config.Parameters
import shuttle.common.ShuttleUOP

//class KanataTracer(fetchWidth: Int,
//                   retireWidth: Int,
//                   vaddrBitsExtended: Int)(implicit p: Parameters)
//  extends BlackBox(Map(
//    "FETCH_WIDTH" -> fetchWidth,
//    "RETIRE_WIDTH" -> retireWidth,
//    "VADDR_BITS_EXTENDED" -> vaddrBitsExtended,
//    "HART_ID_BITS" -> p(MaxHartIdBits),
//  ))
//    with HasBlackBoxResource {
//
//  private def pipelineUopIds = Vec(retireWidth, Valid(UInt(64.W)))
//
//  val io = IO(new Bundle {
//    val clock = Input(Clock())
//    val reset = Input(Bool())
//    val hartid = Input(UInt(p(MaxHartIdBits).W))
//    val s0id = Input(Valid(UInt(64.W)))
//    val s1id = Input(Valid(UInt(64.W)))
//    val s2ids = Input(Vec(fetchWidth, Valid(UInt(64.W))))
//    val s2pcs = Input(Vec(fetchWidth, UInt(vaddrBitsExtended.W)))
//    val rrd, ex, mem, com, wb = Input(pipelineUopIds)
//  })
//
//  addResource("/shuttle/vsrc/KanataTracer.v")
//}

class FrontendStageTracer(implicit p: Parameters)
  extends BlackBox with HasBlackBoxResource {

  val io = IO(new Bundle {
    val clock = Input(Clock())
    val reset = Input(Bool())
    val hartId = Input(UInt(p(MaxHartIdBits).W))
    val portId = Input(UInt(32.W))
    val stageId = Input(UInt(2.W))
    val uopId = Input(Valid(UInt(64.W)))
    val flush = Input(Bool())
  })

  addResource("/shuttle/vsrc/KanataTracer.v")
  addResource("/shuttle/csrc/KanataTracer.cc")
}

class FetchBufferTracer(vaddrBitsExtended: Int)(implicit p: Parameters)
  extends BlackBox(Map(
    "VADDR_BITS_EXTENDED" -> vaddrBitsExtended,
  ))
    with HasBlackBoxResource {

  val io = IO(new Bundle {
    val clock = Input(Clock())
    val reset = Input(Bool())
    val hartId = Input(UInt(p(MaxHartIdBits).W))
    val portId = Input(UInt(32.W))
    val uopId = Input(Valid(UInt(64.W)))
    val uopPc = Input(UInt(vaddrBitsExtended.W))
    val flush = Input(Bool())
  })

  addResource("/shuttle/vsrc/KanataTracer.v")
  addResource("/shuttle/csrc/KanataTracer.cc")
}

class ScalarBackendStageTracer(implicit p: Parameters)
  extends BlackBox
    with HasBlackBoxResource {

  val io = IO(new Bundle {
    val clock = Input(Clock())
    val reset = Input(Bool())
    val hartId = Input(UInt(p(MaxHartIdBits).W))
    val portId = Input(UInt(32.W))
    val stageId = Input(UInt(3.W))
    val uopId = Input(Valid(UInt(64.W)))
    val flush = Input(Bool())
    val wbPending = Input(Bool())
  })

  addResource("/shuttle/vsrc/KanataTracer.v")
  addResource("/shuttle/csrc/KanataTracer.cc")
}

object KanataTracer {
  object ShuttleFrontendStage extends Enumeration {
    val f0, f1 = Value()
  }

  sealed trait ShuttleBackendStage

  object ShuttleBackendStage {
    case object Rrd extends ShuttleBackendStage
    case object Ex extends ShuttleBackendStage
    case object Mem extends ShuttleBackendStage
    case class Com(wbPending: Bool) extends ShuttleBackendStage
    case object Wb extends ShuttleBackendStage
  }

  def frontendStage(stage: ShuttleFrontendStage.Value,
                    clock: Clock,
                    reset: Reset,
                    hartId: UInt,
                    port: UInt,
                    uopId: Valid[UInt],
                    flush: Bool)(implicit p: Parameters): Unit = {
    val m = Module(new FrontendStageTracer())

    ???
  }

  def fetchBuffer(deq: Vec[Decoupled[ShuttleUOP]]): Unit = {
    for (i <- deq.indices) {
      val m = Module(new FetchBufferTracer())

      ???
    }
  }

  /**
   * Adds Kanata tracing for a single scalar backend pipeline stage.
   *
   * @param flush if `true`, the previous instruction is marked as flushed.
   */
  def scalarBackendStage(stage: ShuttleBackendStage,
                         clock: Clock,
                         reset: Reset,
                         hartId: UInt,
                         port: UInt,
                         uop: Valid[ShuttleUOP],
                         flush: Bool)(implicit p: Parameters): Unit = {
    val m = Module(new ScalarBackendStageTracer())

    ???
  }
}