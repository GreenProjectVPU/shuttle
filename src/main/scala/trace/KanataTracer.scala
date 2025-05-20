package shuttle.trace

import chisel3._
import chisel3.util.{HasBlackBoxResource, Valid}
import freechips.rocketchip.tile.MaxHartIdBits
import org.chipsalliance.cde.config.Parameters
import shuttle.common.ShuttleUOP

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

  addResource("/shuttle/vsrc/kanata_tracer.v")
  addResource("/shuttle/csrc/kanata_tracer.cc")
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

  addResource("/shuttle/vsrc/kanata_tracer.v")
  addResource("/shuttle/csrc/kanata_tracer.cc")
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
  })

  addResource("/shuttle/vsrc/kanata_tracer.v")
  addResource("/shuttle/csrc/kanata_tracer.cc")
}

object KanataTracer {
  object ShuttleFrontendStage extends Enumeration {
    val F0, F1, F2 = Value
  }

  sealed abstract class ShuttleBackendStage(val id: Int)

  object ShuttleBackendStage {
    case object Rrd extends ShuttleBackendStage(0)

    case object Ex extends ShuttleBackendStage(1)

    case object Mem extends ShuttleBackendStage(2)

    case object Com extends ShuttleBackendStage(3)
  }

  def frontendStage(stage: ShuttleFrontendStage.Value,
                    clock: Clock,
                    reset: Reset,
                    hartId: UInt,
                    port: UInt,
                    uopId: Valid[UInt],
                    flush: Bool)(implicit p: Parameters): Unit = {
    val m = Module(new FrontendStageTracer)

    m.io.clock := clock
    m.io.reset := reset.asBool
    m.io.hartId := hartId
    m.io.portId := port
    m.io.stageId := stage.id.U
    m.io.uopId := uopId
    m.io.flush := flush
  }

  def fetchBuffer(vaddrBitsExtended: Int,
                  clock: Clock,
                  reset: Reset,
                  hartId: UInt,
                  ram: Vec[Valid[ShuttleUOP]],
                  flush: Bool)(implicit p: Parameters): Unit = {
    for (i <- ram.indices) {
      val m = Module(new FetchBufferTracer(vaddrBitsExtended))

      m.io.clock := clock
      m.io.reset := reset
      m.io.hartId := hartId
      m.io.portId := i.U
      m.io.uopId.bits := ram(i).bits.id
      m.io.uopId.valid := ram(i).valid
      m.io.uopPc := ram(i).bits.pc
      m.io.flush := flush
    }
  }

  def scalarBackendStage(stage: ShuttleBackendStage,
                         clock: Clock,
                         reset: Reset,
                         hartId: UInt,
                         port: UInt,
                         uop: Valid[ShuttleUOP],
                         flush: Bool)(implicit p: Parameters): Unit = {
    val m = Module(new ScalarBackendStageTracer)

    m.io.clock := clock
    m.io.reset := reset
    m.io.hartId := hartId
    m.io.portId := port
    m.io.stageId := stage.id.U
    m.io.uopId.bits := uop.bits.id
    m.io.uopId.valid := uop.valid
    m.io.flush := flush
  }
}
