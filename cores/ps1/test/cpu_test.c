/* CPU and system checks: small programs assembled in C, run instruction by
 * instruction, then registers/memory/COP0 compared with what the R3000A
 * does (PSX-SPX, the R3000A manual). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/ps1.h"
#include "asm.h"

static int failures, checks;
#define CHECK(name, got, want)                                                                         \
  do {                                                                                                 \
    uint64_t g_ = (uint64_t)(got), w_ = (uint64_t)(want);                                              \
    checks++;                                                                                          \
    if (g_ != w_) { failures++; printf("FAIL %-40s got %08llx want %08llx\n", name, (unsigned long long)g_, (unsigned long long)w_); } \
  } while (0)

static PS1 *p;
#define CODE 0x10000u

static void wr(uint32_t a, uint32_t v) { wr32le(p->ram + (a & (RAM_SIZE - 1)), v); }
static uint32_t rd(uint32_t a) { return rd32le(p->ram + (a & (RAM_SIZE - 1))); }

static void fresh(void) {
  memset(p, 0, sizeof *p);
  ps1_reset(p);                   /* no BIOS: HLE mode */
  p->cpu.cop0[C0_SR] = 0;         /* exceptions to 0x80000080 */
}

/* Load a program at CODE and point the CPU at it. */
static void prog(const uint32_t *code, size_t n) {
  for (size_t i = 0; i < n; i++) wr(CODE + (uint32_t)i * 4, code[i]);
  p->cpu.pc = 0x80000000u | CODE;
  p->cpu.next_pc = p->cpu.pc + 4;
}
#define PROG(...) do { const uint32_t c_[] = {__VA_ARGS__}; prog(c_, sizeof c_ / sizeof c_[0]); } while (0)

static void steps(int n) { for (int i = 0; i < n; i++) cpu_step(p); }

/* Exception handler at 0x80000080: k0 = Cause, k1 = EPC, then spin. */
static void handler(void) {
  wr(0x80, MFC0(26, 13));
  wr(0x84, MFC0(27, 14));
  wr(0x88, NOP);
  wr(0x8C, J(0x8000008C));
  wr(0x90, NOP);
}

static uint32_t R(int r) { return p->cpu.gpr[r]; }

int main(void) {
  p = (PS1 *)calloc(1, sizeof(PS1));

  /* ---- ALU ---- */
  fresh();
  PROG(LUI(1, 0x1234), ORI(1, 1, 0x5678), ADDIU(2, 0, -1), ADDU(3, 1, 2), SUBU(4, 1, 2),
       AND(5, 1, 2), OR(6, 0, 1), XOR(7, 1, 2), NOR(8, 0, 0), SLT(9, 2, 0), SLTU(10, 2, 0),
       SLL(11, 1, 4), SRL(12, 2, 28), SRA(13, 2, 28), ADDIU(14, 0, 3), SLLV(15, 1, 14), SRAV(16, 2, 14),
       SLTI(17, 2, 0), SLTIU(18, 0, -1), ANDI(19, 2, 0x00F0), XORI(20, 1, 0xFFFF));
  steps(21);
  CHECK("lui/ori", R(1), 0x12345678);
  CHECK("addiu sign-extends", R(2), 0xFFFFFFFF);
  CHECK("addu", R(3), 0x12345677);
  CHECK("subu", R(4), 0x12345679);
  CHECK("and", R(5), 0x12345678);
  CHECK("or", R(6), 0x12345678);
  CHECK("xor", R(7), 0xEDCBA987);
  CHECK("nor", R(8), 0xFFFFFFFF);
  CHECK("slt signed", R(9), 1);
  CHECK("sltu unsigned", R(10), 0);
  CHECK("sll", R(11), 0x23456780);
  CHECK("srl", R(12), 0xF);
  CHECK("sra", R(13), 0xFFFFFFFF);
  CHECK("sllv", R(15), 0x91A2B3C0);
  CHECK("srav", R(16), 0xFFFFFFFF);
  CHECK("slti", R(17), 1);
  CHECK("sltiu imm sign-extended then unsigned", R(18), 1);
  CHECK("andi zero-extends", R(19), 0xF0);
  CHECK("xori zero-extends", R(20), 0x1234A987);

  /* ---- Load delay slot ---- */
  fresh();
  wr(0x20000, 0xCAFEBABE);
  PROG(LUI(1, 0x8002), ORI(2, 0, 7), LW(2, 0, 1), ADDU(3, 2, 0), ADDU(4, 2, 0));
  steps(5);
  CHECK("load delay: slot sees old value", R(3), 7);
  CHECK("load delay: next sees the load", R(4), 0xCAFEBABE);

  fresh();
  wr(0x20000, 0xCAFEBABE);
  PROG(LUI(1, 0x8002), LW(2, 0, 1), ORI(2, 0, 5), ADDU(3, 2, 0));
  steps(4);
  CHECK("write in the delay slot beats the load", R(3), 5);

  fresh();
  wr(0x20000, 0x11111111);
  wr(0x20004, 0x22222222);
  PROG(LUI(1, 0x8002), LW(2, 0, 1), LW(2, 4, 1), ADDU(3, 2, 0), ADDU(4, 2, 0));
  steps(5);
  CHECK("back-to-back loads: first lands first", R(3), 0x11111111);
  CHECK("back-to-back loads: then the second", R(4), 0x22222222);

  /* LWL/LWR read an unaligned word, merging with each other in flight. */
  fresh();
  wr(0x20000, 0x44332211);
  wr(0x20004, 0x88776655);
  PROG(LUI(1, 0x8002), LWR(2, 1, 1), LWL(2, 4, 1), NOP, ADDU(3, 2, 0));
  steps(5);
  CHECK("lwr+lwl unaligned word", R(3), 0x55443322);

  fresh();
  wr(0x20000, 0);
  wr(0x20004, 0);
  PROG(LUI(1, 0x8002), LUI(2, 0xAABB), ORI(2, 2, 0xCCDD), SWL(2, 1, 1), SWR(2, 5, 1), NOP);
  steps(6);
  CHECK("swl at +1", rd(0x20000), 0x0000AABB);
  CHECK("swr at +5", rd(0x20004), 0xBBCCDD00);

  /* ---- Branches and jumps ---- */
  fresh();
  PROG(ORI(1, 0, 1), BEQ(1, 1, 2), ORI(2, 0, 9), ORI(3, 0, 1), ORI(4, 0, 1));
  steps(4);
  CHECK("branch delay slot runs", R(2), 9);
  CHECK("branch skips", R(3), 0);
  CHECK("branch target runs", R(4), 1);

  fresh();
  PROG(JAL(0x80010010), NOP, NOP, NOP, JR(31), ORI(5, 0, 1), NOP);
  steps(4);
  CHECK("jal sets ra = pc + 8", R(31), 0x80010008);
  CHECK("jr returns", p->cpu.pc, 0x80010008);
  CHECK("jr delay slot", R(5), 1);

  fresh();
  PROG(ORI(1, 0, 1), BLTZAL(1, 4), NOP, NOP);
  steps(3);
  CHECK("bltzal links even when not taken", R(31), 0x8001000C);

  /* ---- Multiply / divide ---- */
  fresh();
  PROG(ADDIU(1, 0, -3), ORI(2, 0, 5), MULT(1, 2), MFLO(3), MFHI(4), MULTU(1, 2), MFHI(5));
  steps(7);
  CHECK("mult lo", R(3), (uint32_t)-15);
  CHECK("mult hi", R(4), 0xFFFFFFFF);
  CHECK("multu hi", R(5), 4);

  fresh();
  PROG(ORI(1, 0, 7), DIV(1, 0), MFLO(2), MFHI(3), ADDIU(4, 0, -7), DIV(4, 0), MFLO(5), DIVU(1, 0), MFLO(6),
       LUI(7, 0x8000), ADDIU(8, 0, -1), DIV(7, 8), MFLO(9), MFHI(10));
  steps(14);
  CHECK("div by zero, positive: lo = -1", R(2), 0xFFFFFFFF);
  CHECK("div by zero: hi = dividend", R(3), 7);
  CHECK("div by zero, negative: lo = 1", R(5), 1);
  CHECK("divu by zero: lo = all ones", R(6), 0xFFFFFFFF);
  CHECK("0x80000000 / -1: lo", R(9), 0x80000000);
  CHECK("0x80000000 / -1: hi", R(10), 0);

  /* ---- Exceptions ---- */
  fresh();
  handler();
  PROG(LUI(1, 0x7FFF), ORI(1, 1, 0xFFFF), ORI(3, 0, 0x55), ADD(3, 1, 1), NOP);
  steps(8);
  CHECK("add overflow: code 12", (R(26) >> 2) & 31, EXC_OV);
  CHECK("add overflow: EPC", R(27), 0x8001000C);
  CHECK("add overflow: no write", R(3), 0x55);

  fresh();
  handler();
  PROG(ORI(1, 0, 1), BEQ(1, 1, 4), SYSCALL, NOP);
  steps(6);
  CHECK("syscall in delay slot: code 8", (R(26) >> 2) & 31, EXC_SYS);
  CHECK("syscall in delay slot: BD", R(26) >> 31, 1);
  CHECK("syscall in delay slot: BT (taken)", (R(26) >> 30) & 1, 1);
  CHECK("syscall in delay slot: EPC = branch", R(27), 0x80010004);

  fresh();
  handler();
  PROG(LUI(1, 0x8002), ORI(1, 1, 2), LW(2, 0, 1), NOP);
  steps(6);
  CHECK("unaligned lw: AdEL", (R(26) >> 2) & 31, EXC_ADEL);
  CHECK("unaligned lw: BadVaddr", p->cpu.cop0[C0_BADVADDR], 0x80020002);

  fresh();
  handler();
  PROG(ORI(1, 0, 0x0D), MTC0(1, 12), NOP, SYSCALL, NOP);
  steps(6);
  CHECK("exception pushes the IE/KU stack", p->cpu.cop0[C0_SR] & 0x3F, 0x34);
  wr(0x8C, RFE);
  wr(0x90, NOP);
  steps(2);
  CHECK("rfe pops it", p->cpu.cop0[C0_SR] & 0x3F, 0x3D);

  fresh();
  handler();
  PROG(BREAK, NOP);
  steps(3);
  CHECK("break: code 9", (R(26) >> 2) & 31, EXC_BP);

  fresh();
  handler();
  PROG(0xFC000000u, NOP);   /* opcode 0x3F: reserved */
  steps(3);
  CHECK("reserved instruction: code 10", (R(26) >> 2) & 31, EXC_RI);

  fresh();
  handler();
  PROG(MFC2(1, 0), NOP);   /* SR.CU2 clear */
  steps(3);
  CHECK("cop2 disabled: CpU", (R(26) >> 2) & 31, EXC_CPU);
  CHECK("cop2 disabled: CE = 2", (R(26) >> 28) & 3, 2);

  fresh();
  PROG(LUI(1, 0x4000), MTC0(1, 12), NOP, LUI(2, 0x1234), MTC2(2, 9), NOP, MFC2(3, 9), NOP, ADDU(4, 3, 0));
  steps(9);
  CHECK("cop2 enabled: register round trip", R(4), 0x12340000);

  /* Interrupts: I_STAT & I_MASK drive Cause.IP2. */
  fresh();
  handler();
  PROG(ORI(1, 0, 0x0401), MTC0(1, 12), NOP, NOP, NOP, NOP);
  p->i_mask = IRQ_VBLANK;
  steps(3);
  irq_raise(p, IRQ_VBLANK);
  steps(5);
  CHECK("interrupt taken: code 0", (R(26) >> 2) & 31, EXC_INT);
  CHECK("interrupt: IP2 pending", (R(26) >> 10) & 1, 1);
  CHECK("interrupt: EPC = next instruction", R(27), 0x8001000C);

  /* Cache isolation: stores don't reach RAM. */
  fresh();
  wr(0x20000, 0x12345678);
  PROG(LUI(1, 0x0001), MTC0(1, 12), NOP, LUI(2, 0x8002), SW(0, 0, 2), MTC0(0, 12), NOP);
  steps(7);
  CHECK("isolated cache: store ignored", rd(0x20000), 0x12345678);

  /* ---- Bus ---- */
  fresh();
  PROG(LUI(1, 0x1F80), ORI(2, 0, 0x1234), SW(2, 0x10, 1), LW(3, 0x10, 1), NOP,
       LUI(4, 0x8020), LW(5, 0, 4), NOP);
  wr(0, 0xDEADBEEF);
  steps(8);
  CHECK("scratchpad", R(3), 0x1234);
  CHECK("RAM mirror at 2 MB", R(5), 0xDEADBEEF);

  fresh();
  handler();
  PROG(LUI(1, 0xBF80), LW(2, 0, 1), NOP);   /* scratchpad through KSEG1 */
  steps(4);
  CHECK("scratchpad through KSEG1: bus error", (R(26) >> 2) & 31, EXC_DBE);

  /* ---- DMA: ordering table clear ---- */
  fresh();
  p->dma.dpcr = 0x08000000;
  dma_write(p, 0x60, 0x0001000C);
  dma_write(p, 0x64, 4);
  dma_write(p, 0x68, 0x11000002);
  CHECK("otc: first entry links back", rd(0x1000C), 0x00010008);
  CHECK("otc: last entry ends the list", rd(0x10000), 0x00FFFFFF);
  CHECK("otc: busy cleared", dma_read(p, 0x68) & 0x01000000, 0);

  /* DICR: completion raises IRQ3 when enabled. */
  fresh();
  p->dma.dpcr = 0x08000000;
  dma_write(p, 0x74, 0x00C00000);   /* master enable + channel 6 */
  dma_write(p, 0x60, 0x00010000);
  dma_write(p, 0x64, 1);
  dma_write(p, 0x68, 0x11000002);
  CHECK("dma irq flag", (dma_read(p, 0x74) >> 30) & 1, 1);
  CHECK("dma irq raised", p->i_stat & IRQ_DMA, IRQ_DMA);
  dma_write(p, 0x74, 0x40C00000);   /* acknowledge */
  CHECK("dma flag acknowledged", dma_read(p, 0x74) >> 24, 0x00);

  /* ---- Timers ---- */
  fresh();
  timer_write(p, 0x28, 100);       /* timer 2 target */
  timer_write(p, 0x24, 0x0058);    /* reset at target, IRQ at target, repeat */
  timers_tick(p, 150);
  CHECK("timer counts and resets at target", timer_read(p, 0x20), 50);
  CHECK("timer irq", p->i_stat & IRQ_TMR2, IRQ_TMR2);
  CHECK("reached-target flag", (timer_read(p, 0x24) >> 11) & 1, 1);
  CHECK("flag clears on read", (timer_read(p, 0x24) >> 11) & 1, 0);

  fresh();
  timer_write(p, 0x24, 0x0200);    /* timer 2, system clock / 8 */
  timers_tick(p, 80);
  CHECK("timer 2 at clock/8", timer_read(p, 0x20), 10);

  /* ---- GPU: fill, CPU->VRAM, VRAM->CPU ---- */
  fresh();
  gp0_write(p, 0x02FF0000);        /* fill, colour 0xBBGGRR = blue */
  gp0_write(p, 0x00100010);        /* x 16, y 16 */
  gp0_write(p, 0x00020010);        /* 16 x 2 */
  CHECK("fill", p->gpu.vram[16 * VRAM_W + 16], 0x7C00);
  gp0_write(p, 0xA0000000);
  gp0_write(p, 0x00000000);
  gp0_write(p, 0x00010002);        /* 2 x 1 at 0,0 */
  gp0_write(p, 0x22221111);
  CHECK("cpu->vram pixel 0", p->gpu.vram[0], 0x1111);
  CHECK("cpu->vram pixel 1", p->gpu.vram[1], 0x2222);
  CHECK("gpustat ready for commands", (gpu_stat(p) >> 26) & 1, 1);
  gp0_write(p, 0xC0000000);
  gp0_write(p, 0x00000000);
  gp0_write(p, 0x00010002);
  CHECK("vram->cpu ready", (gpu_stat(p) >> 27) & 1, 1);
  CHECK("vram->cpu read", gpu_read(p), 0x22221111);
  gp0_write(p, 0x80000000);        /* vram copy 0,0 -> 4,0 */
  gp0_write(p, 0x00000000);
  gp0_write(p, 0x00000004);
  gp0_write(p, 0x00010002);
  CHECK("vram->vram copy", p->gpu.vram[5], 0x2222);
  gp1_write(p, 0x08000001);        /* 320 wide */
  CHECK("gpustat hres", (gpu_stat(p) >> 17) & 3, 1);
  gp1_write(p, 0x10000007);
  CHECK("gpu version", gpu_read(p), 2);

  /* ---- Controller ---- */
  fresh();
  p->pad[0].connected = true;
  p->pad[0].buttons = 1 << 14;     /* Cross */
  sio_write(p, 0xA, 2, 0x1003);    /* select port 1, ACK IRQ on, TX on */
  uint8_t got[5];
  static const uint8_t tx[5] = {0x01, 0x42, 0x00, 0x00, 0x00};
  for (int i = 0; i < 5; i++) {
    sio_write(p, 0x0, 1, tx[i]);
    got[i] = (uint8_t)sio_read(p, 0x0, 1);
    if (i < 4) {
      p->cpu.cycles += 2000;
      sio_tick(p);
    }
  }
  CHECK("pad: hi-z first byte", got[0], 0xFF);
  CHECK("pad: digital pad id", got[1], 0x41);
  CHECK("pad: 5A", got[2], 0x5A);
  CHECK("pad: buttons low (none)", got[3], 0xFF);
  CHECK("pad: buttons high (cross)", got[4], 0xBF);
  CHECK("pad: /ACK raised IRQ7", p->i_stat & IRQ_PAD, IRQ_PAD);

  /* ---- PS-EXE without a BIOS: printf through the BIOS call vector ---- */
  {
    static uint8_t exe[0x800 + 0x100];
    memset(exe, 0, sizeof exe);
    memcpy(exe, "PS-X EXE", 8);
    wr32le(exe + 0x10, 0x80010000);   /* pc */
    wr32le(exe + 0x18, 0x80010000);   /* load address */
    wr32le(exe + 0x1C, 0x100);
    wr32le(exe + 0x30, 0x801FFF00);   /* stack */
    const uint32_t code[] = {
        LUI(4, 0x8001), ORI(4, 4, 0x0080),     /* a0 = format */
        ORI(5, 0, 42), ORI(6, 0, 42),           /* a1 = a2 = 42 */
        ORI(8, 0, 0xA0), JALR(31, 8), ORI(9, 0, 0x3F),   /* printf (A0:3F) */
        J(0x8001001C), NOP,
    };
    for (size_t i = 0; i < sizeof code / sizeof code[0]; i++) wr32le(exe + 0x800 + i * 4, code[i]);
    memcpy(exe + 0x880, "hello %d %04x\n", 15);
    memset(p, 0, sizeof *p);
    CHECK("exe accepted", ps1_load_exe(p, exe, sizeof exe), 1);
    ps1_reset(p);
    CHECK("exe pc", p->cpu.pc, 0x80010000);
    CHECK("exe sp", p->cpu.gpr[29], 0x801FFF00);
    steps(20);
    CHECK("printf output", strcmp(p->tty, "hello 42 002a\n"), 0);
  }

  printf("cpu: %d/%d checks passed\n", checks - failures, checks);
  free(p);
  return failures ? 1 : 0;
}
