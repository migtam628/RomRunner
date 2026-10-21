/*
 * CPU tests: small programs assembled in C, run on the core, checked against
 * the behaviour the MIPS III / VR4300 manuals specify.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/n64.h"
#include "asm.h"

static N64 *n;
static uint32_t idx;
static uint32_t halt_ok, halt_exc;
static int failures, checks;

#define CODE_PA 0x1000u
#define CODE_VA 0x80001000u
#define EXC_HALT 0x80000F00u

#define CHECK(name, got, want)                                                                         \
  do {                                                                                                 \
    uint64_t g_ = (uint64_t)(got), w_ = (uint64_t)(want);                                              \
    checks++;                                                                                          \
    if (g_ != w_) {                                                                                    \
      failures++;                                                                                      \
      printf("FAIL %-40s got %016llx want %016llx\n", name, (unsigned long long)g_, (unsigned long long)w_); \
    }                                                                                                  \
  } while (0)

static void put(uint32_t pa, uint32_t w) { wr32be(n->rdram + pa, w); }

/* Exception handlers: general vector sets r30=1, TLB refill sets r30=2, then
 * both stop at EXC_HALT. */
static void default_handlers(void) {
  put(0x000, ORI(30, 0, 2)); put(0x004, J(EXC_HALT)); put(0x008, NOP);
  put(0x080, ORI(30, 0, 3)); put(0x084, J(EXC_HALT)); put(0x088, NOP);
  put(0x180, ORI(30, 0, 1)); put(0x184, J(EXC_HALT)); put(0x188, NOP);
  put(0xF00, J(EXC_HALT)); put(0xF04, NOP);
}

static void begin(void) {
  memset(n, 0, sizeof *n);
  cpu_reset(n);
  n->cpu.cp0[CP0_STATUS] = ST_CU0 | ST_CU1 | ST_FR; /* kernel, no BEV/ERL */
  default_handlers();
  idx = 0;
}

static uint32_t here(void) { return CODE_VA + idx * 4; }
static void emit(uint32_t w) { put(CODE_PA + idx * 4, w); idx++; }

static void finish(void) {
  halt_ok = here();
  emit(J(halt_ok));
  emit(NOP);
}

/* Runs until a halt loop; returns 0 normal halt, 1 exception halt, -1 timeout. */
static int run(uint32_t max_steps) {
  halt_exc = EXC_HALT;
  n->cpu.pc = sext32(CODE_VA);
  n->cpu.next_pc = n->cpu.pc + 4;
  for (uint32_t i = 0; i < max_steps; i++) {
    uint32_t pc = (uint32_t)n->cpu.pc;
    if (pc == halt_ok) return 0;
    if (pc == halt_exc) return 1;
    cpu_step(n);
  }
  return -1;
}

static uint64_t R(int r) { return n->cpu.gpr[r]; }
static uint32_t exc_code(void) { return (uint32_t)(n->cpu.cp0[CP0_CAUSE] >> 2) & 31; }

#define LI(r, v) do { emit(LI_HI(r, v)); emit(LI_LO(r, v)); } while (0)

/* ------------------------------------------------------------------ */

static void test_alu(void) {
  begin();
  emit(ADDIU(1, 0, -1));
  LI(2, 0x80001234u);
  emit(ADDU(3, 2, 1));
  emit(DADDU(4, 2, 1));
  LI(5, 0x7FFFFFFFu);
  emit(ADDIU(6, 5, 1));
  emit(ADDIU(7, 0, 1));
  emit(DSLL32(7, 7, 0));
  emit(DSRA32(8, 2, 0));
  emit(SLT(9, 2, 0));
  emit(SLTU(10, 2, 0));
  emit(SLTIU(11, 0, -1));
  emit(NOR(12, 0, 0));
  emit(SRL(13, 1, 4));
  emit(SRA(14, 2, 4));
  emit(ANDI(15, 1, 0xF0F0));
  emit(XORI(16, 0, 0xFFFF));
  emit(DSRL32(17, 1, 28));
  emit(DADDIU(18, 0, -2));
  emit(DSUBU(19, 0, 18));
  emit(SLL(20, 5, 1));
  emit(ADDIU(21, 0, 4));
  emit(DSLLV(22, 7, 21));
  emit(SLTI(23, 2, -5));
  finish();
  CHECK("alu halts", run(1000), 0);
  CHECK("addiu sign-extends", R(1), 0xFFFFFFFFFFFFFFFFull);
  CHECK("lui/ori", R(2), 0xFFFFFFFF80001234ull);
  CHECK("addu wraps at 32 bits", R(3), 0xFFFFFFFF80001233ull);
  CHECK("daddu", R(4), 0xFFFFFFFF80001233ull);
  CHECK("addiu does not trap", R(6), 0xFFFFFFFF80000000ull);
  CHECK("dsll32", R(7), 0x100000000ull);
  CHECK("dsra32", R(8), 0xFFFFFFFFFFFFFFFFull);
  CHECK("slt negative", R(9), 1);
  CHECK("sltu", R(10), 0);
  CHECK("sltiu sign-extended imm", R(11), 1);
  CHECK("nor", R(12), 0xFFFFFFFFFFFFFFFFull);
  CHECK("srl is 32-bit", R(13), 0x0FFFFFFFull);
  CHECK("sra", R(14), 0xFFFFFFFFF8000123ull);
  CHECK("andi zero-extends", R(15), 0xF0F0);
  CHECK("xori zero-extends", R(16), 0xFFFF);
  CHECK("dsrl", R(17), 0xF);
  CHECK("daddiu", R(18), (uint64_t)-2);
  CHECK("dsubu", R(19), 2);
  CHECK("sll sign-extends result", R(20), 0xFFFFFFFFFFFFFFFEull);
  CHECK("dsllv", R(22), 0x1000000000ull);
  CHECK("slti", R(23), 1);
}

static void test_overflow(void) {
  begin();
  LI(5, 0x7FFFFFFFu);
  emit(ADDIU(6, 0, 1));
  emit(ADDIU(7, 0, 0x55));
  uint32_t at = here();
  emit(ADD(7, 5, 6));
  finish();
  CHECK("add overflow traps", run(1000), 1);
  CHECK("overflow handler ran", R(30), 1);
  CHECK("overflow exception code", exc_code(), EXC_OV);
  CHECK("overflow EPC", n->cpu.cp0[CP0_EPC], sext32(at));
  CHECK("overflow leaves rd", R(7), 0x55);
  CHECK("EXL set", n->cpu.cp0[CP0_STATUS] & ST_EXL, ST_EXL);

  begin();
  LI(1, 0x7FFFFFFFu);
  emit(DSLL32(1, 1, 0));
  emit(ORI(1, 1, 0xFFFF));     /* 0x7FFFFFFF0000FFFF */
  emit(DADDI(2, 1, 0x7FFF));   /* fine */
  LI(3, 0x7FFFFFFFu);
  emit(DSLL32(3, 3, 0));
  emit(DSRL(4, 1, 0));
  emit(ADDIU(5, 0, -1));
  emit(DSRL32(5, 5, 0));       /* 0x00000000FFFFFFFF */
  emit(OR(3, 3, 5));           /* 0x7FFFFFFFFFFFFFFF */
  emit(DADDI(6, 3, 1));        /* overflows */
  finish();
  CHECK("daddi overflow traps", run(1000), 1);
  CHECK("daddi overflow code", exc_code(), EXC_OV);
  CHECK("daddi no-overflow result", R(2), 0x7FFFFFFF00017FFEull);
}

static void test_branches(void) {
  begin();
  emit(BEQ(0, 0, 2));
  emit(ADDIU(3, 0, 5));        /* delay slot runs */
  emit(ADDIU(4, 0, 9));        /* skipped */
  emit(ADDIU(5, 0, 1));        /* target */
  emit(BNEL(0, 0, 2));
  emit(ADDIU(6, 0, 7));        /* likely, not taken: delay slot skipped */
  emit(ADDIU(8, 0, 3));
  emit(BEQL(0, 0, 2));
  emit(ADDIU(9, 0, 4));        /* likely, taken: runs */
  emit(ADDIU(10, 0, 1));       /* skipped */
  uint32_t jal_at = here();
  emit(JAL(jal_at + 12));
  emit(ADDIU(11, 0, 2));       /* delay slot */
  emit(ADDIU(12, 0, 1));       /* skipped by the jump */
  emit(BGEZAL(0, 1));
  uint32_t bgezal_at = here() - 4;
  emit(NOP);
  emit(NOP);
  uint32_t target = here() + 20; /* LI (2), JR, delay, skipped */
  LI(20, target);
  emit(JR(20));
  emit(ADDIU(13, 0, 6));
  emit(ADDIU(14, 0, 1));       /* skipped */
  emit(ADDIU(15, 0, 2));       /* target */
  emit(BLTZL(0, 2));           /* 0 < 0 is false: skip delay */
  emit(ADDIU(16, 0, 1));
  emit(ADDIU(17, 0, 1));
  finish();
  CHECK("branches halt", run(1000), 0);
  CHECK("beq delay slot executes", R(3), 5);
  CHECK("beq skips fallthrough", R(4), 0);
  CHECK("beq reaches target", R(5), 1);
  CHECK("bnel not taken nullifies delay", R(6), 0);
  CHECK("bnel continues", R(8), 3);
  CHECK("beql taken runs delay", R(9), 4);
  CHECK("beql skips", R(10), 0);
  CHECK("jal delay slot", R(11), 2);
  CHECK("jal jumps", R(12), 0);
  CHECK("bgezal links", R(31), sext32(bgezal_at + 8));
  CHECK("jr delay slot", R(13), 6);
  CHECK("jr skips", R(14), 0);
  CHECK("jr target", R(15), 2);
  CHECK("bltzl nullifies", R(16), 0);
  CHECK("after bltzl", R(17), 1);
  (void)jal_at;
}

static void test_muldiv(void) {
  begin();
  emit(ADDIU(1, 0, -3));
  emit(ADDIU(2, 0, 7));
  emit(MULT(1, 2)); emit(MFLO(3)); emit(MFHI(4));
  emit(MULTU(1, 2)); emit(MFLO(5)); emit(MFHI(6));
  emit(DIV(2, 1)); emit(MFLO(7)); emit(MFHI(8));
  emit(DIV(2, 0)); emit(MFLO(9)); emit(MFHI(10));
  emit(DIV(1, 0)); emit(MFLO(11)); emit(MFHI(12));
  emit(DIVU(2, 0)); emit(MFLO(13)); emit(MFHI(14));
  emit(LUI(15, 0x8000)); emit(ADDIU(16, 0, -1));
  emit(DIV(15, 16)); emit(MFLO(17)); emit(MFHI(18));
  emit(DMULTU(16, 16)); emit(MFLO(19)); emit(MFHI(20));
  emit(DMULT(16, 16)); emit(MFLO(21)); emit(MFHI(22));
  emit(ADDIU(23, 0, 1)); emit(DSLL32(23, 23, 0));   /* 1 << 32 */
  emit(DSUBU(24, 0, 23));                            /* -(1 << 32) */
  emit(DMULT(23, 24)); emit(MFLO(25)); emit(MFHI(26));
  emit(ADDIU(27, 0, -7)); emit(ADDIU(28, 0, 2));
  emit(DDIV(27, 28)); emit(MFLO(29)); emit(MFHI(30));
  finish();
  CHECK("muldiv halts", run(1000), 0);
  CHECK("mult lo", R(3), (uint64_t)-21);
  CHECK("mult hi", R(4), (uint64_t)-1);
  CHECK("multu lo", R(5), 0xFFFFFFFFFFFFFFEBull);
  CHECK("multu hi", R(6), 6);
  CHECK("div quotient", R(7), (uint64_t)-2);
  CHECK("div remainder", R(8), 1);
  CHECK("div by zero (positive) lo", R(9), (uint64_t)-1);
  CHECK("div by zero hi", R(10), 7);
  CHECK("div by zero (negative) lo", R(11), 1);
  CHECK("div by zero (negative) hi", R(12), (uint64_t)-3);
  CHECK("divu by zero lo", R(13), (uint64_t)-1);
  CHECK("divu by zero hi", R(14), 7);
  CHECK("div INT_MIN/-1 lo", R(17), 0xFFFFFFFF80000000ull);
  CHECK("div INT_MIN/-1 hi", R(18), 0);
  CHECK("dmultu lo", R(19), 1);
  CHECK("dmultu hi", R(20), 0xFFFFFFFFFFFFFFFEull);
  CHECK("dmult -1*-1 lo", R(21), 1);
  CHECK("dmult -1*-1 hi", R(22), 0);
  CHECK("dmult 2^32*-2^32 lo", R(25), 0);
  CHECK("dmult 2^32*-2^32 hi", R(26), (uint64_t)-1);
  CHECK("ddiv truncates", R(29), (uint64_t)-3);
  CHECK("ddiv remainder sign", R(30), (uint64_t)-1);
}

static void test_loadstore(void) {
  begin();
  static const uint8_t data[16] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
                                   0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00};
  memcpy(n->rdram + 0x2000, data, 16);
  LI(1, 0x80002000u);
  emit(LWL(2, 1, 1)); emit(LWR(2, 4, 1));
  emit(LW(3, 4, 1));
  emit(LB(4, 7, 1)); emit(LBU(5, 7, 1));
  emit(LH(6, 8, 1)); emit(LHU(7, 8, 1));
  emit(LWU(8, 8, 1));
  emit(LDL(9, 3, 1)); emit(LDR(9, 10, 1));
  emit(LD(10, 0, 1));
  LI(11, 0xAABBCCDDu);
  LI(12, 0x80002020u);
  emit(SWL(11, 1, 12)); emit(SWR(11, 4, 12));
  LI(13, 0x01020304u);
  emit(DSLL32(13, 13, 0));
  LI(14, 0x05060708u);
  emit(OR(13, 13, 14));
  emit(SDL(13, 0x13, 12)); emit(SDR(13, 0x1A, 12));
  emit(SB(11, 0x40, 12));
  emit(SH(11, 0x42, 12));
  emit(SD(13, 0x48, 12));
  /* Reading an unaligned word is an address error. */
  emit(LW(15, 2, 1));
  finish();
  CHECK("loadstore address error", run(1000), 1);
  CHECK("adel code", exc_code(), EXC_ADEL);
  CHECK("badvaddr", n->cpu.cp0[CP0_BADVADDR], 0xFFFFFFFF80002002ull);
  CHECK("lwl/lwr unaligned word", R(2), 0x22334455);
  CHECK("lw", R(3), 0x55667788);
  CHECK("lb sign-extends", R(4), 0xFFFFFFFFFFFFFF88ull);
  CHECK("lbu", R(5), 0x88);
  CHECK("lh sign-extends", R(6), 0xFFFFFFFFFFFF99AAull);
  CHECK("lhu", R(7), 0x99AA);
  CHECK("lwu zero-extends", R(8), 0x99AABBCCull);
  CHECK("ldl/ldr unaligned dword", R(9), 0x445566778899AABBull);
  CHECK("ld", R(10), 0x1122334455667788ull);
  const uint8_t *m = n->rdram + 0x2020;
  CHECK("swl/swr bytes", rd32be(m + 1), 0xAABBCCDD);
  CHECK("swl/swr keeps neighbours", m[0] | m[5], 0);
  CHECK("sdl/sdr bytes", rd64be(m + 0x13), 0x0102030405060708ull);
  CHECK("sb", m[0x40], 0xDD);
  CHECK("sh", rd16be(m + 0x42), 0xCCDD);
  CHECK("sd", rd64be(m + 0x48), 0x0102030405060708ull);
}

static void test_tlb(void) {
  begin();
  emit(MTC0(0, CP0_PAGEMASK));
  emit(MTC0(0, CP0_ENTRYHI));
  LI(1, (0x400u << 6) | 7u);   /* PFN 0x400 (PA 0x400000), dirty, valid, global */
  emit(MTC0(1, CP0_ENTRYLO0));
  emit(MTC0(0, CP0_ENTRYLO1));  /* odd page invalid */
  emit(MTC0(0, CP0_INDEX));
  emit(TLBWI);
  LI(2, 0xA0400010u);
  LI(3, 0x12345678u);
  emit(SW(3, 0, 2));
  emit(LW(4, 0x10, 0));         /* VA 0x10 -> PA 0x400010 */
  emit(ADDIU(5, 0, 0x5555));
  emit(SW(5, 0x14, 0));
  emit(TLBP);
  emit(MFC0(6, CP0_INDEX));
  LI(7, 0x00004000u);
  emit(MTC0(7, CP0_ENTRYHI));
  emit(TLBP);
  emit(MFC0(8, CP0_INDEX));
  emit(MTC0(0, CP0_ENTRYHI));
  emit(LW(9, 0x5000, 0));       /* no entry: TLB refill */
  finish();
  CHECK("tlb refill halts", run(1000), 1);
  CHECK("mapped load", R(4), 0x12345678);
  CHECK("mapped store", rd32be(n->rdram + 0x400014), 0x5555);
  CHECK("tlbp finds entry", R(6), 0);
  CHECK("tlbp miss sets P", R(8), 0xFFFFFFFF80000000ull);
  CHECK("refill vector used", R(30), 2);
  CHECK("refill code TLBL", exc_code(), EXC_TLBL);
  CHECK("refill badvaddr", n->cpu.cp0[CP0_BADVADDR], 0x5000);
  CHECK("refill entryhi vpn2", n->cpu.cp0[CP0_ENTRYHI] & ~0x1FFFull, 0x4000);

  /* Odd half of the pair is invalid: TLB invalid goes to the general vector. */
  begin();
  emit(MTC0(0, CP0_PAGEMASK));
  emit(MTC0(0, CP0_ENTRYHI));
  LI(1, (0x400u << 6) | 7u);
  emit(MTC0(1, CP0_ENTRYLO0));
  emit(MTC0(0, CP0_ENTRYLO1));
  emit(MTC0(0, CP0_INDEX));
  emit(TLBWI);
  emit(SW(0, 0x1000, 0));
  finish();
  CHECK("tlb invalid halts", run(1000), 1);
  CHECK("invalid uses general vector", R(30), 1);
  CHECK("invalid store code TLBS", exc_code(), EXC_TLBS);

  /* Clean (not dirty) page: a store raises TLB modification. */
  begin();
  emit(MTC0(0, CP0_PAGEMASK));
  emit(MTC0(0, CP0_ENTRYHI));
  LI(1, (0x400u << 6) | 3u);   /* valid + global, not dirty */
  emit(MTC0(1, CP0_ENTRYLO0));
  emit(MTC0(1, CP0_ENTRYLO1));
  emit(MTC0(0, CP0_INDEX));
  emit(TLBWI);
  emit(LW(2, 0, 0));
  emit(SW(2, 0, 0));
  finish();
  CHECK("tlb mod halts", run(1000), 1);
  CHECK("tlb mod code", exc_code(), EXC_MOD);
}

static void test_exceptions(void) {
  begin();
  uint32_t at = here();
  emit(SYSCALL);
  finish();
  CHECK("syscall", run(100), 1);
  CHECK("syscall code", exc_code(), EXC_SYS);
  CHECK("syscall epc", n->cpu.cp0[CP0_EPC], sext32(at));

  begin();
  emit(BREAK);
  finish();
  run(100);
  CHECK("break code", exc_code(), EXC_BP);

  begin();
  emit(TEQ(0, 0));
  finish();
  run(100);
  CHECK("teq code", exc_code(), EXC_TR);

  begin();
  emit(0x70000000u); /* opcode 0x1C: reserved on the VR4300 */
  finish();
  run(100);
  CHECK("reserved instruction", exc_code(), EXC_RI);

  begin();
  uint32_t br = here();
  emit(BEQ(0, 0, 1));
  emit(SYSCALL);           /* in the delay slot */
  finish();
  run(100);
  CHECK("delay slot epc is the branch", n->cpu.cp0[CP0_EPC], sext32(br));
  CHECK("delay slot sets BD", n->cpu.cp0[CP0_CAUSE] >> 31 & 1, 1);

  /* A handler that skips the faulting instruction and returns with ERET. */
  begin();
  put(0x180, MFC0(26, CP0_EPC));
  put(0x184, ADDIU(26, 26, 4));
  put(0x188, MTC0(26, CP0_EPC));
  put(0x18C, ADDIU(29, 29, 1));
  put(0x190, ERET);
  emit(SYSCALL);
  emit(ADDIU(5, 0, 1));
  emit(SYSCALL);
  emit(ADDIU(6, 0, 2));
  finish();
  CHECK("eret returns", run(1000), 0);
  CHECK("after first eret", R(5), 1);
  CHECK("after second eret", R(6), 2);
  CHECK("handler ran twice", R(29), 2);
  CHECK("eret clears EXL", n->cpu.cp0[CP0_STATUS] & ST_EXL, 0);

  /* COP1 unusable when CU1 is clear. */
  begin();
  n->cpu.cp0[CP0_STATUS] = ST_CU0;
  emit(MTC1(0, 1));
  finish();
  run(100);
  CHECK("cop1 unusable code", exc_code(), EXC_CPU);
  CHECK("cop1 unusable CE", (n->cpu.cp0[CP0_CAUSE] >> 28) & 3, 1);
}

static void test_llsc(void) {
  begin();
  LI(1, 0x80003000u);
  emit(ADDIU(2, 0, 41));
  emit(SW(2, 0, 1));
  emit(LL(3, 0, 1));
  emit(ADDIU(3, 3, 1));
  emit(SC(3, 0, 1));
  emit(LW(4, 0, 1));
  finish();
  CHECK("llsc halts", run(1000), 0);
  CHECK("sc succeeds after ll", R(3), 1);
  CHECK("sc stored", R(4), 42);

  begin();
  LI(1, 0x80003000u);
  emit(ADDIU(3, 0, 9));
  emit(SC(3, 0, 1));
  finish();
  run(1000);
  CHECK("sc without ll fails", R(3), 0);
}

static void test_interrupts(void) {
  /* Count == Compare raises IP7. */
  begin();
  emit(MTC0(0, CP0_COUNT));
  emit(ADDIU(1, 0, 50));
  emit(MTC0(1, CP0_COMPARE));
  LI(2, 0x34008001u);          /* CU0|CU1|FR, IM7, IE */
  emit(MTC0(2, CP0_STATUS));
  emit(BEQ(0, 0, -1));
  emit(NOP);
  finish();
  CHECK("timer interrupt taken", run(10000), 1);
  CHECK("interrupt code", exc_code(), EXC_INT);
  CHECK("IP7 pending", (n->cpu.cp0[CP0_CAUSE] >> 15) & 1, 1);

  /* MI (RCP) interrupt arrives on IP2 when unmasked in MI_MASK. */
  begin();
  LI(2, 0x34000401u);          /* IM2, IE */
  emit(MTC0(2, CP0_STATUS));
  LI(3, 0xA430000Cu);          /* MI_MASK */
  emit(ADDIU(4, 0, 0x80));     /* set VI mask */
  emit(SW(4, 0, 3));
  emit(BEQ(0, 0, -1));
  emit(NOP);
  finish();
  n->cpu.pc = sext32(CODE_VA);
  n->cpu.next_pc = n->cpu.pc + 4;
  for (int i = 0; i < 20; i++) cpu_step(n);
  n64_raise_mi(n, MI_INTR_VI);
  int r = 0;
  for (int i = 0; i < 100 && !r; i++) {
    if ((uint32_t)n->cpu.pc == EXC_HALT) r = 1;
    cpu_step(n);
  }
  CHECK("mi interrupt taken", r, 1);
  CHECK("IP2 pending", (n->cpu.cp0[CP0_CAUSE] >> 10) & 1, 1);
}

static void test_fpu(void) {
  begin();
  emit(LUI(1, 0x3FC0)); emit(MTC1(1, 1));          /* 1.5f */
  emit(LUI(2, 0x4010)); emit(MTC1(2, 2));          /* 2.25f */
  emit(ADD_S(3, 1, 2)); emit(MFC1(3, 3));
  emit(DIV_S(4, 2, 1)); emit(MFC1(4, 4));
  emit(LUI(5, 0x4020)); emit(MTC1(5, 5));          /* 2.5f */
  emit(ROUND_W_S(6, 5)); emit(MFC1(6, 6));
  emit(TRUNC_W_S(7, 5)); emit(MFC1(7, 7));
  emit(CEIL_W_S(8, 5)); emit(MFC1(8, 8));
  emit(FLOOR_W_S(9, 5)); emit(MFC1(9, 9));
  emit(LUI(10, 0xC020)); emit(MTC1(10, 10));       /* -2.5f */
  emit(ROUND_W_S(10, 10)); emit(MFC1(10, 10));
  emit(LUI(11, 0x4060)); emit(MTC1(11, 11));       /* 3.5f */
  emit(ROUND_W_S(11, 11)); emit(MFC1(11, 11));
  emit(ADDIU(12, 0, 2)); emit(CTC1(12, 31));       /* round toward +inf */
  LI(13, 0x40066666u); emit(MTC1(13, 13));         /* 2.1f */
  emit(CVT_W_S(13, 13)); emit(MFC1(13, 13));
  emit(CFC1(14, 31));
  emit(CTC1(0, 31));
  emit(C_LT_S(1, 2));
  emit(BC1T(2));
  emit(NOP);
  emit(ADDIU(15, 0, 1));                           /* skipped */
  emit(ADDIU(16, 0, 1));
  emit(CVT_D_S(8, 1)); emit(DMFC1(17, 8));
  emit(ADDIU(18, 0, 7)); emit(MTC1(18, 9));
  emit(CVT_S_W(10, 9)); emit(MFC1(18, 10));
  emit(DMTC1(17, 12));
  emit(MUL_D(14, 12, 12)); emit(DMFC1(19, 14));    /* 1.5 * 1.5 = 2.25 */
  emit(SQRT_D(16, 14)); emit(DMFC1(20, 16));       /* 1.5 */
  emit(C_EQ_D(16, 12));
  emit(BC1F(2));
  emit(NOP);
  emit(ADDIU(21, 0, 1));
  emit(NOP);
  finish();
  CHECK("fpu halts", run(1000), 0);
  CHECK("add.s", R(3), 0x40700000);
  CHECK("div.s", R(4), 0x3FC00000);
  CHECK("round.w.s 2.5 -> 2 (even)", R(6), 2);
  CHECK("trunc.w.s", R(7), 2);
  CHECK("ceil.w.s", R(8), 3);
  CHECK("floor.w.s", R(9), 2);
  CHECK("round.w.s -2.5 -> -2", R(10), (uint64_t)-2);
  CHECK("round.w.s 3.5 -> 4", R(11), 4);
  CHECK("cvt.w.s honours rounding mode", R(13), 3);
  CHECK("fcr31 rounding mode", R(14) & 3, 2);
  CHECK("bc1t taken", R(15), 0);
  CHECK("after bc1t", R(16), 1);
  CHECK("cvt.d.s", R(17), 0x3FF8000000000000ull);
  CHECK("cvt.s.w", R(18), 0x40E00000);
  CHECK("mul.d", R(19), 0x4002000000000000ull);
  CHECK("sqrt.d", R(20), 0x3FF8000000000000ull);
  CHECK("c.eq.d + bc1f not taken", R(21), 1);

  /* FR=0: odd registers are the upper halves of even ones. */
  begin();
  n->cpu.cp0[CP0_STATUS] = ST_CU0 | ST_CU1;
  emit(LUI(1, 0x3FC0)); emit(MTC1(1, 1));
  emit(LUI(2, 0x4010)); emit(MTC1(2, 0));
  emit(DMFC1(3, 0));
  emit(DMFC1(4, 1));     /* odd number reads the even register */
  emit(MFC1(5, 1));
  finish();
  CHECK("fr0 halts", run(1000), 0);
  CHECK("fr0 pair", R(3), 0x3FC0000040100000ull);
  CHECK("fr0 odd 64-bit read", R(4), 0x3FC0000040100000ull);
  CHECK("fr0 odd 32-bit read", R(5), 0x3FC00000);
}

int main(void) {
  n = (N64 *)calloc(1, sizeof(N64));
  test_alu();
  test_overflow();
  test_branches();
  test_muldiv();
  test_loadstore();
  test_tlb();
  test_exceptions();
  test_llsc();
  test_interrupts();
  test_fpu();
  printf("cpu: %d/%d checks passed\n", checks - failures, checks);
  free(n);
  return failures ? 1 : 0;
}
