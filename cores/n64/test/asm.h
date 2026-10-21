/* A tiny MIPS III encoder for writing test programs in C. */
#ifndef RRN64_TEST_ASM_H
#define RRN64_TEST_ASM_H

#include <stdint.h>

static inline uint32_t enc_r(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t sa, uint32_t fn) {
  return rs << 21 | rt << 16 | rd << 11 | sa << 6 | fn;
}
static inline uint32_t enc_i(uint32_t op, uint32_t rs, uint32_t rt, int32_t imm) {
  return op << 26 | rs << 21 | rt << 16 | ((uint32_t)imm & 0xFFFF);
}

/* SPECIAL */
#define SLL(d, t, sa)   enc_r(0, t, d, sa, 0x00)
#define SRL(d, t, sa)   enc_r(0, t, d, sa, 0x02)
#define SRA(d, t, sa)   enc_r(0, t, d, sa, 0x03)
#define SLLV(d, t, s)   enc_r(s, t, d, 0, 0x04)
#define JR(s)           enc_r(s, 0, 0, 0, 0x08)
#define JALR(d, s)      enc_r(s, 0, d, 0, 0x09)
#define SYSCALL         enc_r(0, 0, 0, 0, 0x0C)
#define BREAK           enc_r(0, 0, 0, 0, 0x0D)
#define MFHI(d)         enc_r(0, 0, d, 0, 0x10)
#define MFLO(d)         enc_r(0, 0, d, 0, 0x12)
#define DSLLV(d, t, s)  enc_r(s, t, d, 0, 0x14)
#define MULT(s, t)      enc_r(s, t, 0, 0, 0x18)
#define MULTU(s, t)     enc_r(s, t, 0, 0, 0x19)
#define DIV(s, t)       enc_r(s, t, 0, 0, 0x1A)
#define DIVU(s, t)      enc_r(s, t, 0, 0, 0x1B)
#define DMULT(s, t)     enc_r(s, t, 0, 0, 0x1C)
#define DMULTU(s, t)    enc_r(s, t, 0, 0, 0x1D)
#define DDIV(s, t)      enc_r(s, t, 0, 0, 0x1E)
#define DDIVU(s, t)     enc_r(s, t, 0, 0, 0x1F)
#define ADD(d, s, t)    enc_r(s, t, d, 0, 0x20)
#define ADDU(d, s, t)   enc_r(s, t, d, 0, 0x21)
#define SUB(d, s, t)    enc_r(s, t, d, 0, 0x22)
#define SUBU(d, s, t)   enc_r(s, t, d, 0, 0x23)
#define AND(d, s, t)    enc_r(s, t, d, 0, 0x24)
#define OR(d, s, t)     enc_r(s, t, d, 0, 0x25)
#define XOR(d, s, t)    enc_r(s, t, d, 0, 0x26)
#define NOR(d, s, t)    enc_r(s, t, d, 0, 0x27)
#define SLT(d, s, t)    enc_r(s, t, d, 0, 0x2A)
#define SLTU(d, s, t)   enc_r(s, t, d, 0, 0x2B)
#define DADD(d, s, t)   enc_r(s, t, d, 0, 0x2C)
#define DADDU(d, s, t)  enc_r(s, t, d, 0, 0x2D)
#define DSUBU(d, s, t)  enc_r(s, t, d, 0, 0x2F)
#define TEQ(s, t)       enc_r(s, t, 0, 0, 0x34)
#define DSLL(d, t, sa)  enc_r(0, t, d, sa, 0x38)
#define DSRL(d, t, sa)  enc_r(0, t, d, sa, 0x3A)
#define DSRA(d, t, sa)  enc_r(0, t, d, sa, 0x3B)
#define DSLL32(d, t, sa) enc_r(0, t, d, sa, 0x3C)
#define DSRL32(d, t, sa) enc_r(0, t, d, sa, 0x3E)
#define DSRA32(d, t, sa) enc_r(0, t, d, sa, 0x3F)
#define NOP             0u

/* REGIMM */
#define BLTZ(s, off)    enc_i(1, s, 0x00, off)
#define BGEZ(s, off)    enc_i(1, s, 0x01, off)
#define BLTZL(s, off)   enc_i(1, s, 0x02, off)
#define BGEZAL(s, off)  enc_i(1, s, 0x11, off)

/* I-type */
#define J(a)            (2u << 26 | (((uint32_t)(a) >> 2) & 0x3FFFFFF))
#define JAL(a)          (3u << 26 | (((uint32_t)(a) >> 2) & 0x3FFFFFF))
#define BEQ(s, t, off)  enc_i(0x04, s, t, off)
#define BNE(s, t, off)  enc_i(0x05, s, t, off)
#define BLEZ(s, off)    enc_i(0x06, s, 0, off)
#define BGTZ(s, off)    enc_i(0x07, s, 0, off)
#define ADDI(t, s, i)   enc_i(0x08, s, t, i)
#define ADDIU(t, s, i)  enc_i(0x09, s, t, i)
#define SLTI(t, s, i)   enc_i(0x0A, s, t, i)
#define SLTIU(t, s, i)  enc_i(0x0B, s, t, i)
#define ANDI(t, s, i)   enc_i(0x0C, s, t, i)
#define ORI(t, s, i)    enc_i(0x0D, s, t, i)
#define XORI(t, s, i)   enc_i(0x0E, s, t, i)
#define LUI(t, i)       enc_i(0x0F, 0, t, i)
#define BEQL(s, t, off) enc_i(0x14, s, t, off)
#define BNEL(s, t, off) enc_i(0x15, s, t, off)
#define DADDI(t, s, i)  enc_i(0x18, s, t, i)
#define DADDIU(t, s, i) enc_i(0x19, s, t, i)
#define LDL(t, o, s)    enc_i(0x1A, s, t, o)
#define LDR(t, o, s)    enc_i(0x1B, s, t, o)
#define LB(t, o, s)     enc_i(0x20, s, t, o)
#define LH(t, o, s)     enc_i(0x21, s, t, o)
#define LWL(t, o, s)    enc_i(0x22, s, t, o)
#define LW(t, o, s)     enc_i(0x23, s, t, o)
#define LBU(t, o, s)    enc_i(0x24, s, t, o)
#define LHU(t, o, s)    enc_i(0x25, s, t, o)
#define LWR(t, o, s)    enc_i(0x26, s, t, o)
#define LWU(t, o, s)    enc_i(0x27, s, t, o)
#define SB(t, o, s)     enc_i(0x28, s, t, o)
#define SH(t, o, s)     enc_i(0x29, s, t, o)
#define SWL(t, o, s)    enc_i(0x2A, s, t, o)
#define SW(t, o, s)     enc_i(0x2B, s, t, o)
#define SDL(t, o, s)    enc_i(0x2C, s, t, o)
#define SDR(t, o, s)    enc_i(0x2D, s, t, o)
#define SWR(t, o, s)    enc_i(0x2E, s, t, o)
#define LL(t, o, s)     enc_i(0x30, s, t, o)
#define LWC1(t, o, s)   enc_i(0x31, s, t, o)
#define LD(t, o, s)     enc_i(0x37, s, t, o)
#define SC(t, o, s)     enc_i(0x38, s, t, o)
#define SWC1(t, o, s)   enc_i(0x39, s, t, o)
#define SD(t, o, s)     enc_i(0x3F, s, t, o)

/* COP0 */
#define MFC0(t, d)      (0x10u << 26 | 0x00u << 21 | (uint32_t)(t) << 16 | (uint32_t)(d) << 11)
#define MTC0(t, d)      (0x10u << 26 | 0x04u << 21 | (uint32_t)(t) << 16 | (uint32_t)(d) << 11)
#define TLBWI           0x42000002u
#define TLBP            0x42000008u
#define TLBR            0x42000001u
#define ERET            0x42000018u

/* COP1 */
#define MFC1(t, fs)     (0x11u << 26 | 0x00u << 21 | (uint32_t)(t) << 16 | (uint32_t)(fs) << 11)
#define DMFC1(t, fs)    (0x11u << 26 | 0x01u << 21 | (uint32_t)(t) << 16 | (uint32_t)(fs) << 11)
#define CFC1(t, fs)     (0x11u << 26 | 0x02u << 21 | (uint32_t)(t) << 16 | (uint32_t)(fs) << 11)
#define MTC1(t, fs)     (0x11u << 26 | 0x04u << 21 | (uint32_t)(t) << 16 | (uint32_t)(fs) << 11)
#define DMTC1(t, fs)    (0x11u << 26 | 0x05u << 21 | (uint32_t)(t) << 16 | (uint32_t)(fs) << 11)
#define CTC1(t, fs)     (0x11u << 26 | 0x06u << 21 | (uint32_t)(t) << 16 | (uint32_t)(fs) << 11)
#define BC1T(off)       (0x11u << 26 | 0x08u << 21 | 1u << 16 | ((uint32_t)(off) & 0xFFFF))
#define BC1F(off)       (0x11u << 26 | 0x08u << 21 | 0u << 16 | ((uint32_t)(off) & 0xFFFF))
#define FPU(fmt, ft, fs, fd, fn) (0x11u << 26 | (uint32_t)(fmt) << 21 | (uint32_t)(ft) << 16 | (uint32_t)(fs) << 11 | (uint32_t)(fd) << 6 | (fn))
#define FMT_S 16
#define FMT_D 17
#define FMT_W 20
#define ADD_S(fd, fs, ft)   FPU(FMT_S, ft, fs, fd, 0x00)
#define MUL_D(fd, fs, ft)   FPU(FMT_D, ft, fs, fd, 0x02)
#define DIV_S(fd, fs, ft)   FPU(FMT_S, ft, fs, fd, 0x03)
#define SQRT_D(fd, fs)      FPU(FMT_D, 0, fs, fd, 0x04)
#define ROUND_W_S(fd, fs)   FPU(FMT_S, 0, fs, fd, 0x0C)
#define TRUNC_W_S(fd, fs)   FPU(FMT_S, 0, fs, fd, 0x0D)
#define CEIL_W_S(fd, fs)    FPU(FMT_S, 0, fs, fd, 0x0E)
#define FLOOR_W_S(fd, fs)   FPU(FMT_S, 0, fs, fd, 0x0F)
#define CVT_W_S(fd, fs)     FPU(FMT_S, 0, fs, fd, 0x24)
#define CVT_D_S(fd, fs)     FPU(FMT_S, 0, fs, fd, 0x21)
#define CVT_S_W(fd, fs)     FPU(FMT_W, 0, fs, fd, 0x20)
#define C_LT_S(fs, ft)      FPU(FMT_S, ft, fs, 0, 0x3C)
#define C_EQ_D(fs, ft)      FPU(FMT_D, ft, fs, 0, 0x32)

/* Loads a 32-bit constant (sign-extended), two instructions. */
#define LI_HI(r, v)     LUI(r, (int32_t)((uint32_t)(v) >> 16))
#define LI_LO(r, v)     ORI(r, r, (int32_t)((uint32_t)(v) & 0xFFFF))

#endif
