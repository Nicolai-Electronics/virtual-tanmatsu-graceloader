#include "cpu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ==== Guest memory access =====================================================
// Guest addresses are real host pointers (see guest_mem.h), so we just cast
// and dereference. Host is little-endian x86-64, same as the RV32 guest, so
// no byte-swapping is needed either.

static inline uint8_t  ld8(uint32_t a) {
    return *(uint8_t *)(uintptr_t)a;
}
static inline uint16_t ld16(uint32_t a) {
    uint16_t v;
    memcpy(&v, (void *)(uintptr_t)a, 2);
    return v;
}
static inline uint32_t ld32(uint32_t a) {
    uint32_t v;
    memcpy(&v, (void *)(uintptr_t)a, 4);
    return v;
}
static inline void st8(uint32_t a, uint8_t v) {
    *(uint8_t *)(uintptr_t)a = v;
}
static inline void st16(uint32_t a, uint16_t v) {
    memcpy((void *)(uintptr_t)a, &v, 2);
}
static inline void st32(uint32_t a, uint32_t v) {
    memcpy((void *)(uintptr_t)a, &v, 4);
}

static inline float f32_from_bits(uint32_t b) {
    float f;
    memcpy(&f, &b, 4);
    return f;
}
static inline uint32_t f32_to_bits(float f) {
    uint32_t b;
    memcpy(&b, &f, 4);
    return b;
}

static inline uint32_t sext(uint32_t value, unsigned bits) {
    uint32_t m = 1u << (bits - 1);
    return (value ^ m) - m;
}

void cpu_reset(cpu_t *cpu) {
    memset(cpu, 0, sizeof(*cpu));
    cpu->fcsr = 0;
}

static inline uint32_t getx(cpu_t *cpu, unsigned r) {
    return r == 0 ? 0 : cpu->x[r];
}
static inline void setx(cpu_t *cpu, unsigned r, uint32_t v) {
    if (r != 0) {
        cpu->x[r] = v;
    }
}

// ==== 32-bit instruction execution ============================================

static bool exec32(cpu_t *cpu, uint32_t instr, uint32_t pc, uint32_t *next_pc) {
    uint32_t opcode = instr & 0x7F;
    uint32_t rd     = (instr >> 7) & 0x1F;
    uint32_t funct3 = (instr >> 12) & 0x7;
    uint32_t rs1    = (instr >> 15) & 0x1F;
    uint32_t rs2    = (instr >> 20) & 0x1F;
    uint32_t funct7 = (instr >> 25) & 0x7F;

    int32_t  imm_i  = (int32_t)instr >> 20;
    uint32_t imm_s  = ((instr >> 25) << 5) | ((instr >> 7) & 0x1F);
    int32_t  simm_s = (int32_t)sext(imm_s, 12);
    uint32_t imm_b =
        (((instr >> 31) & 1) << 12) | (((instr >> 7) & 1) << 11) | (((instr >> 25) & 0x3F) << 5) |
        (((instr >> 8) & 0xF) << 1);
    int32_t  simm_b = (int32_t)sext(imm_b, 13);
    uint32_t imm_u  = instr & 0xFFFFF000u;
    uint32_t imm_j  = (((instr >> 31) & 1) << 20) | (((instr >> 12) & 0xFF) << 12) | (((instr >> 20) & 1) << 11) |
                      (((instr >> 21) & 0x3FF) << 1);
    int32_t simm_j = (int32_t)sext(imm_j, 21);

    uint32_t a = getx(cpu, rs1);
    uint32_t b = getx(cpu, rs2);

    switch (opcode) {
        case 0x37:  // LUI
            setx(cpu, rd, imm_u);
            return true;
        case 0x17:  // AUIPC
            setx(cpu, rd, pc + imm_u);
            return true;
        case 0x6F:  // JAL
            setx(cpu, rd, pc + 4);
            *next_pc = pc + (uint32_t)simm_j;
            return true;
        case 0x67:  // JALR
            if (funct3 != 0) return false;
            {
                uint32_t target = (a + (uint32_t)imm_i) & ~1u;
                setx(cpu, rd, pc + 4);
                *next_pc = target;
            }
            return true;
        case 0x63:  // Branches
            switch (funct3) {
                case 0: if (a == b) *next_pc = pc + (uint32_t)simm_b; return true;           // BEQ
                case 1: if (a != b) *next_pc = pc + (uint32_t)simm_b; return true;           // BNE
                case 4: if ((int32_t)a < (int32_t)b) *next_pc = pc + (uint32_t)simm_b; return true;  // BLT
                case 5: if ((int32_t)a >= (int32_t)b) *next_pc = pc + (uint32_t)simm_b; return true; // BGE
                case 6: if (a < b) *next_pc = pc + (uint32_t)simm_b; return true;            // BLTU
                case 7: if (a >= b) *next_pc = pc + (uint32_t)simm_b; return true;           // BGEU
                default: return false;
            }
        case 0x03: {  // Loads
            uint32_t addr = a + (uint32_t)imm_i;
            switch (funct3) {
                case 0: setx(cpu, rd, sext(ld8(addr), 8)); return true;   // LB
                case 1: setx(cpu, rd, sext(ld16(addr), 16)); return true; // LH
                case 2: setx(cpu, rd, ld32(addr)); return true;           // LW
                case 4: setx(cpu, rd, ld8(addr)); return true;            // LBU
                case 5: setx(cpu, rd, ld16(addr)); return true;           // LHU
                default: return false;
            }
        }
        case 0x23: {  // Stores
            uint32_t addr = a + (uint32_t)simm_s;
            switch (funct3) {
                case 0: st8(addr, (uint8_t)b); return true;   // SB
                case 1: st16(addr, (uint16_t)b); return true; // SH
                case 2: st32(addr, b); return true;           // SW
                default: return false;
            }
        }
        case 0x13:  // Immediate ALU
            switch (funct3) {
                case 0: setx(cpu, rd, a + (uint32_t)imm_i); return true;  // ADDI
                case 2: setx(cpu, rd, (int32_t)a < imm_i); return true;   // SLTI
                case 3: setx(cpu, rd, a < (uint32_t)imm_i); return true;  // SLTIU
                case 4: setx(cpu, rd, a ^ (uint32_t)imm_i); return true;  // XORI
                case 6: setx(cpu, rd, a | (uint32_t)imm_i); return true;  // ORI
                case 7: setx(cpu, rd, a & (uint32_t)imm_i); return true;  // ANDI
                case 1:                                                  // SLLI
                    if (funct7 != 0) return false;
                    setx(cpu, rd, a << (rs2 & 0x1F));
                    return true;
                case 5:  // SRLI / SRAI
                    if (funct7 == 0x20) {
                        setx(cpu, rd, (uint32_t)((int32_t)a >> (rs2 & 0x1F)));
                    } else if (funct7 == 0) {
                        setx(cpu, rd, a >> (rs2 & 0x1F));
                    } else {
                        return false;
                    }
                    return true;
                default: return false;
            }
        case 0x33:  // Register ALU (+ M extension)
            if (funct7 == 0x01) {
                switch (funct3) {
                    case 0: setx(cpu, rd, a * b); return true;  // MUL
                    case 1: setx(cpu, rd, (uint32_t)(((int64_t)(int32_t)a * (int64_t)(int32_t)b) >> 32)); return true; // MULH
                    case 2: setx(cpu, rd, (uint32_t)(((int64_t)(int32_t)a * (int64_t)(uint32_t)b) >> 32)); return true; // MULHSU
                    case 3: setx(cpu, rd, (uint32_t)(((uint64_t)a * (uint64_t)b) >> 32)); return true; // MULHU
                    case 4:  // DIV
                        if (b == 0) setx(cpu, rd, 0xFFFFFFFFu);
                        else if (a == 0x80000000u && b == 0xFFFFFFFFu) setx(cpu, rd, 0x80000000u);
                        else setx(cpu, rd, (uint32_t)((int32_t)a / (int32_t)b));
                        return true;
                    case 5:  // DIVU
                        setx(cpu, rd, b == 0 ? 0xFFFFFFFFu : a / b);
                        return true;
                    case 6:  // REM
                        if (b == 0) setx(cpu, rd, a);
                        else if (a == 0x80000000u && b == 0xFFFFFFFFu) setx(cpu, rd, 0);
                        else setx(cpu, rd, (uint32_t)((int32_t)a % (int32_t)b));
                        return true;
                    case 7:  // REMU
                        setx(cpu, rd, b == 0 ? a : a % b);
                        return true;
                    default: return false;
                }
            }
            switch (funct3) {
                case 0: setx(cpu, rd, funct7 == 0x20 ? a - b : a + b); return true;  // SUB/ADD
                case 1: setx(cpu, rd, a << (b & 0x1F)); return true;                 // SLL
                case 2: setx(cpu, rd, (int32_t)a < (int32_t)b); return true;         // SLT
                case 3: setx(cpu, rd, a < b); return true;                           // SLTU
                case 4: setx(cpu, rd, a ^ b); return true;                           // XOR
                case 5:                                                             // SRL/SRA
                    setx(cpu, rd, funct7 == 0x20 ? (uint32_t)((int32_t)a >> (b & 0x1F)) : a >> (b & 0x1F));
                    return true;
                case 6: setx(cpu, rd, a | b); return true;  // OR
                case 7: setx(cpu, rd, a & b); return true;  // AND
                default: return false;
            }
        case 0x0F:  // FENCE / FENCE.I -- no-op (single hart, coherent memory)
            return true;
        case 0x73:  // SYSTEM: ECALL/EBREAK/CSR*
            if (funct3 == 0) {
                // ECALL/EBREAK: not used by this ABI (no OS syscalls); treat
                // as a no-op so an accidental hit doesn't crash the host.
                fprintf(stderr, "[cpu] ECALL/EBREAK at 0x%08x (ignored)\n", pc);
                return true;
            } else {
                uint32_t csr = instr >> 20;
                uint32_t old = 0;
                // Only fcsr/frm/fflags are meaningfully backed; everything
                // else (cycle/instret/time counters, ...) reads as 0 and
                // discards writes.
                if (csr == 0x001) old = cpu->fcsr & 0x1F;
                else if (csr == 0x002) old = (cpu->fcsr >> 5) & 0x7;
                else if (csr == 0x003) old = cpu->fcsr;
                else if (csr == 0xC00 || csr == 0xC01 || csr == 0xC02) old = (uint32_t)cpu->instret;

                uint32_t src = (funct3 & 0x4) ? rs1 : a;  // immediate variants encode value in rs1 field
                uint32_t result;
                switch (funct3 & 0x3) {
                    case 1: result = src; break;                  // CSRRW(I)
                    case 2: result = old | src; break;            // CSRRS(I)
                    case 3: result = old & ~src; break;           // CSRRC(I)
                    default: return false;
                }
                if (csr == 0x001) cpu->fcsr = (cpu->fcsr & ~0x1Fu) | (result & 0x1F);
                else if (csr == 0x002) cpu->fcsr = (cpu->fcsr & ~0xE0u) | ((result & 0x7) << 5);
                else if (csr == 0x003) cpu->fcsr = result & 0xFF;
                setx(cpu, rd, old);
                return true;
            }
        case 0x2F: {  // A extension (Zaamo + Zalrsc), word-size only
            uint32_t addr = a;
            uint32_t op   = funct7 >> 2;
            switch (op) {
                case 0x02:  // LR.W
                    setx(cpu, rd, ld32(addr));
                    cpu->lr_valid = true;
                    cpu->lr_addr  = addr;
                    return true;
                case 0x03:  // SC.W
                    if (cpu->lr_valid && cpu->lr_addr == addr) {
                        st32(addr, b);
                        setx(cpu, rd, 0);
                    } else {
                        setx(cpu, rd, 1);
                    }
                    cpu->lr_valid = false;
                    return true;
                default: {
                    uint32_t old = ld32(addr);
                    uint32_t nv;
                    switch (op) {
                        case 0x01: nv = b; break;                                              // AMOSWAP
                        case 0x00: nv = old + b; break;                                         // AMOADD
                        case 0x04: nv = old ^ b; break;                                         // AMOXOR
                        case 0x0C: nv = old & b; break;                                         // AMOAND
                        case 0x08: nv = old | b; break;                                         // AMOOR
                        case 0x10: nv = (int32_t)old < (int32_t)b ? old : b; break;              // AMOMIN
                        case 0x14: nv = (int32_t)old > (int32_t)b ? old : b; break;              // AMOMAX
                        case 0x18: nv = old < b ? old : b; break;                                // AMOMINU
                        case 0x1C: nv = old > b ? old : b; break;                                // AMOMAXU
                        default: return false;
                    }
                    st32(addr, nv);
                    setx(cpu, rd, old);
                    return true;
                }
            }
        }
        case 0x07:  // FLW
            if (funct3 != 2) return false;
            cpu->f[rd] = f32_from_bits(ld32(a + (uint32_t)imm_i));
            return true;
        case 0x27:  // FSW
            if (funct3 != 2) return false;
            st32(a + (uint32_t)simm_s, f32_to_bits(cpu->f[rs2]));
            return true;
        case 0x43: case 0x47: case 0x4B: case 0x4F: {  // FMADD/FMSUB/FNMSUB/FNMADD.S
            uint32_t rs3 = instr >> 27;
            float    r1 = cpu->f[rs1], r2 = cpu->f[rs2], r3 = cpu->f[rs3];
            float    result;
            switch (opcode) {
                case 0x43: result = r1 * r2 + r3; break;
                case 0x47: result = r1 * r2 - r3; break;
                case 0x4B: result = -(r1 * r2) + r3; break;
                default: result = -(r1 * r2) - r3; break;
            }
            cpu->f[rd] = result;
            return true;
        }
        case 0x53: {  // FP ALU (OP-FP), single precision only (funct7 bit0=0)
            switch (funct7) {
                case 0x00: cpu->f[rd] = cpu->f[rs1] + cpu->f[rs2]; return true;  // FADD.S
                case 0x04: cpu->f[rd] = cpu->f[rs1] - cpu->f[rs2]; return true;  // FSUB.S
                case 0x08: cpu->f[rd] = cpu->f[rs1] * cpu->f[rs2]; return true;  // FMUL.S
                case 0x0C: cpu->f[rd] = cpu->f[rs1] / cpu->f[rs2]; return true;  // FDIV.S
                case 0x2C: cpu->f[rd] = __builtin_sqrtf(cpu->f[rs1]); return true;  // FSQRT.S
                case 0x10: {
                    uint32_t s1 = f32_to_bits(cpu->f[rs1]), s2 = f32_to_bits(cpu->f[rs2]);
                    switch (funct3) {
                        case 0: cpu->f[rd] = f32_from_bits((s1 & 0x7FFFFFFFu) | (s2 & 0x80000000u)); return true;  // FSGNJ.S
                        case 1: cpu->f[rd] = f32_from_bits((s1 & 0x7FFFFFFFu) | (~s2 & 0x80000000u)); return true; // FSGNJN.S
                        case 2: cpu->f[rd] = f32_from_bits((s1 & 0x7FFFFFFFu) | ((s1 ^ s2) & 0x80000000u)); return true; // FSGNJX.S
                        default: return false;
                    }
                }
                case 0x14:
                    if (funct3 == 0) { float x = cpu->f[rs1], y = cpu->f[rs2]; cpu->f[rd] = (x < y) ? x : (y < x ? y : (x == x ? x : y)); return true; } // FMIN.S
                    if (funct3 == 1) { float x = cpu->f[rs1], y = cpu->f[rs2]; cpu->f[rd] = (x > y) ? x : (y > x ? y : (x == x ? x : y)); return true; } // FMAX.S
                    return false;
                case 0x60:  // FCVT.W.S / FCVT.WU.S
                    if (rs2 == 0) { setx(cpu, rd, (uint32_t)(int32_t)cpu->f[rs1]); return true; }
                    if (rs2 == 1) { setx(cpu, rd, (uint32_t)cpu->f[rs1]); return true; }
                    return false;
                case 0x68:  // FCVT.S.W / FCVT.S.WU
                    if (rs2 == 0) { cpu->f[rd] = (float)(int32_t)a; return true; }
                    if (rs2 == 1) { cpu->f[rd] = (float)a; return true; }
                    return false;
                case 0x70:  // FMV.X.W / FCLASS.S
                    if (funct3 == 0) { setx(cpu, rd, f32_to_bits(cpu->f[rs1])); return true; }
                    if (funct3 == 1) {
                        float v = cpu->f[rs1];
                        uint32_t bits_ = f32_to_bits(v);
                        uint32_t cls = 0;
                        if (v != v) cls = 1u << 9;  // NaN (quiet, approximated)
                        else if (v == 0.0f) cls = (bits_ & 0x80000000u) ? (1u << 3) : (1u << 4);
                        else if (bits_ & 0x80000000u) cls = (v == -__builtin_inff()) ? (1u << 0) : (1u << 1);
                        else cls = (v == __builtin_inff()) ? (1u << 7) : (1u << 6);
                        setx(cpu, rd, cls);
                        return true;
                    }
                    return false;
                case 0x78:  // FMV.W.X
                    if (funct3 != 0) return false;
                    cpu->f[rd] = f32_from_bits(a);
                    return true;
                case 0x50:  // FEQ/FLT/FLE.S
                    switch (funct3) {
                        case 2: setx(cpu, rd, cpu->f[rs1] == cpu->f[rs2]); return true;  // FEQ.S
                        case 1: setx(cpu, rd, cpu->f[rs1] < cpu->f[rs2]); return true;   // FLT.S
                        case 0: setx(cpu, rd, cpu->f[rs1] <= cpu->f[rs2]); return true;  // FLE.S
                        default: return false;
                    }
                default: return false;
            }
        }
        default: return false;
    }
}

// ==== Compressed (RVC) instruction execution ==================================

static inline unsigned creg(unsigned bits3) {
    return bits3 + 8;
}

static bool exec16(cpu_t *cpu, uint16_t c, uint32_t pc, uint32_t *next_pc) {
    unsigned op     = c & 0x3;
    unsigned funct3 = (c >> 13) & 0x7;

    if (op == 0) {
        unsigned rd_ = creg((c >> 2) & 0x7);
        unsigned rs1_ = creg((c >> 7) & 0x7);
        switch (funct3) {
            case 0: {  // C.ADDI4SPN
                unsigned nz = ((c >> 7) & 0x30) | ((c >> 1) & 0x3C0) | ((c >> 4) & 0x4) | ((c >> 2) & 0x8);
                if (nz == 0) return false;
                setx(cpu, rd_, getx(cpu, 2) + nz);
                return true;
            }
            case 2: {  // C.LW
                unsigned off = ((c << 1) & 0x40) | ((c >> 7) & 0x38) | ((c >> 4) & 0x4);
                setx(cpu, rd_, ld32(getx(cpu, rs1_) + off));
                return true;
            }
            case 3: {  // C.FLW
                unsigned off = ((c << 1) & 0x40) | ((c >> 7) & 0x38) | ((c >> 4) & 0x4);
                cpu->f[rd_] = f32_from_bits(ld32(getx(cpu, rs1_) + off));
                return true;
            }
            case 6: {  // C.SW
                unsigned off = ((c << 1) & 0x40) | ((c >> 7) & 0x38) | ((c >> 4) & 0x4);
                st32(getx(cpu, rs1_) + off, getx(cpu, creg((c >> 2) & 0x7)));
                return true;
            }
            case 7: {  // C.FSW
                unsigned off = ((c << 1) & 0x40) | ((c >> 7) & 0x38) | ((c >> 4) & 0x4);
                st32(getx(cpu, rs1_) + off, f32_to_bits(cpu->f[creg((c >> 2) & 0x7)]));
                return true;
            }
            default: return false;
        }
    } else if (op == 1) {
        unsigned rd = (c >> 7) & 0x1F;
        switch (funct3) {
            case 0: {  // C.NOP / C.ADDI
                int32_t imm = (int32_t)sext(((c >> 7) & 0x20) | ((c >> 2) & 0x1F), 6);
                setx(cpu, rd, getx(cpu, rd) + (uint32_t)imm);
                return true;
            }
            case 1: {  // C.JAL (RV32 only)
                int32_t off = (int32_t)sext(
                    (((c >> 1) & 0x800)) | (((c >> 7) & 0x10)) | (((c >> 1) & 0x300)) | (((c << 2) & 0x400)) |
                        (((c >> 1) & 0x40)) | (((c << 1) & 0x80)) | (((c >> 2) & 0x0E)) | (((c << 3) & 0x20)),
                    12
                );
                setx(cpu, 1, pc + 2);
                *next_pc = pc + (uint32_t)off;
                return true;
            }
            case 2: {  // C.LI
                int32_t imm = (int32_t)sext(((c >> 7) & 0x20) | ((c >> 2) & 0x1F), 6);
                setx(cpu, rd, (uint32_t)imm);
                return true;
            }
            case 3: {
                if (rd == 2) {  // C.ADDI16SP
                    int32_t imm = (int32_t)sext(
                        ((c >> 3) & 0x200) | ((c >> 2) & 0x10) | ((c << 1) & 0x40) | ((c << 4) & 0x180) |
                            ((c << 3) & 0x20),
                        10
                    );
                    if (imm == 0) return false;
                    setx(cpu, 2, getx(cpu, 2) + (uint32_t)imm);
                } else {  // C.LUI
                    if (rd == 0) return false;
                    uint32_t nz = ((c << 5) & 0x20000) | ((c << 10) & 0x1F000);
                    if (nz == 0) return false;
                    setx(cpu, rd, sext(nz, 18));
                }
                return true;
            }
            case 4: {
                unsigned grp  = (c >> 10) & 0x3;
                unsigned rd_  = creg((c >> 7) & 0x7);
                unsigned rs2_ = creg((c >> 2) & 0x7);
                if (grp == 0) {  // C.SRLI
                    unsigned sh = ((c >> 7) & 0x20) | ((c >> 2) & 0x1F);
                    setx(cpu, rd_, getx(cpu, rd_) >> sh);
                    return true;
                } else if (grp == 1) {  // C.SRAI
                    unsigned sh = ((c >> 7) & 0x20) | ((c >> 2) & 0x1F);
                    setx(cpu, rd_, (uint32_t)((int32_t)getx(cpu, rd_) >> sh));
                    return true;
                } else if (grp == 2) {  // C.ANDI
                    int32_t imm = (int32_t)sext(((c >> 7) & 0x20) | ((c >> 2) & 0x1F), 6);
                    setx(cpu, rd_, getx(cpu, rd_) & (uint32_t)imm);
                    return true;
                } else {  // grp == 3: C.SUB/C.XOR/C.OR/C.AND (and RV64-only C.SUBW/ADDW -- bit12 must be 0)
                    if ((c >> 12) & 1) return false;
                    unsigned sel = (c >> 5) & 0x3;
                    uint32_t rv  = getx(cpu, rd_), bv = getx(cpu, rs2_);
                    switch (sel) {
                        case 0: setx(cpu, rd_, rv - bv); return true;
                        case 1: setx(cpu, rd_, rv ^ bv); return true;
                        case 2: setx(cpu, rd_, rv | bv); return true;
                        case 3: setx(cpu, rd_, rv & bv); return true;
                    }
                    return false;
                }
            }
            case 5: {  // C.J
                int32_t off = (int32_t)sext(
                    (((c >> 1) & 0x800)) | (((c >> 7) & 0x10)) | (((c >> 1) & 0x300)) | (((c << 2) & 0x400)) |
                        (((c >> 1) & 0x40)) | (((c << 1) & 0x80)) | (((c >> 2) & 0x0E)) | (((c << 3) & 0x20)),
                    12
                );
                *next_pc = pc + (uint32_t)off;
                return true;
            }
            case 6:    // C.BEQZ
            case 7: {  // C.BNEZ
                unsigned rs1_ = creg((c >> 7) & 0x7);
                int32_t  off  = (int32_t)sext(
                    (((c >> 4) & 0x100)) | (((c >> 7) & 0x18)) | (((c << 1) & 0xC0)) | (((c >> 2) & 0x06)) |
                        (((c << 3) & 0x20)),
                    9
                );
                bool cond = getx(cpu, rs1_) == 0;
                if (funct3 == 7) cond = !cond;
                if (cond) *next_pc = pc + (uint32_t)off;
                return true;
            }
            default: return false;
        }
    } else if (op == 2) {
        unsigned rd = (c >> 7) & 0x1F;
        switch (funct3) {
            case 0: {  // C.SLLI
                unsigned sh = ((c >> 7) & 0x20) | ((c >> 2) & 0x1F);
                setx(cpu, rd, getx(cpu, rd) << sh);
                return true;
            }
            case 2: {  // C.LWSP
                if (rd == 0) return false;
                unsigned off = ((c >> 7) & 0x20) | ((c >> 2) & 0x1C) | ((c << 4) & 0xC0);
                setx(cpu, rd, ld32(getx(cpu, 2) + off));
                return true;
            }
            case 3: {  // C.FLWSP
                unsigned off = ((c >> 7) & 0x20) | ((c >> 2) & 0x1C) | ((c << 4) & 0xC0);
                cpu->f[rd]   = f32_from_bits(ld32(getx(cpu, 2) + off));
                return true;
            }
            case 4: {
                unsigned rs2 = (c >> 2) & 0x1F;
                if (!((c >> 12) & 1)) {
                    if (rs2 == 0) {  // C.JR
                        if (rd == 0) return false;
                        *next_pc = getx(cpu, rd) & ~1u;
                    } else {  // C.MV
                        setx(cpu, rd, getx(cpu, rs2));
                    }
                } else {
                    if (rd == 0 && rs2 == 0) {  // C.EBREAK
                        fprintf(stderr, "[cpu] C.EBREAK at 0x%08x (ignored)\n", pc);
                    } else if (rs2 == 0) {  // C.JALR
                        uint32_t target = getx(cpu, rd) & ~1u;
                        setx(cpu, 1, pc + 2);
                        *next_pc = target;
                    } else {  // C.ADD
                        setx(cpu, rd, getx(cpu, rd) + getx(cpu, rs2));
                    }
                }
                return true;
            }
            case 6: {  // C.SWSP
                unsigned off = ((c >> 7) & 0x3C) | ((c >> 1) & 0xC0);
                st32(getx(cpu, 2) + off, getx(cpu, (c >> 2) & 0x1F));
                return true;
            }
            case 7: {  // C.FSWSP
                unsigned off = ((c >> 7) & 0x3C) | ((c >> 1) & 0xC0);
                st32(getx(cpu, 2) + off, f32_to_bits(cpu->f[(c >> 2) & 0x1F]));
                return true;
            }
            default: return false;
        }
    }
    return false;
}

static int s_trace = -1;

bool cpu_step(cpu_t *cpu) {
    if (cpu_trap_hook(cpu, cpu->pc)) {
        return true;
    }

    if (s_trace < 0) {
        s_trace = getenv("CPU_TRACE") != NULL;
    }
    if (s_trace) {
        fprintf(stderr, "pc=0x%08x ra=0x%08x sp=0x%08x a0=0x%08x\n", cpu->pc, cpu->x[1], cpu->x[2], cpu->x[10]);
        fflush(stderr);
    }

    uint32_t pc  = cpu->pc;
    uint16_t low = ld16(pc);
    uint32_t next_pc;
    bool     ok;

    if ((low & 0x3) != 0x3) {
        next_pc = pc + 2;
        ok      = exec16(cpu, low, pc, &next_pc);
    } else {
        uint32_t instr = (uint32_t)low | ((uint32_t)ld16(pc + 2) << 16);
        next_pc        = pc + 4;
        ok              = exec32(cpu, instr, pc, &next_pc);
    }

    cpu->x[0] = 0;
    cpu->instret++;

    if (!ok) {
        fprintf(stderr, "[cpu] illegal/unimplemented instruction at 0x%08x\n", pc);
        return false;
    }

    cpu->pc = next_pc;
    return true;
}

void cpu_run_until_return(cpu_t *cpu) {
    while (!cpu->halted && cpu->pc != CPU_CALL_SENTINEL) {
        if (!cpu_step(cpu)) {
            cpu->halted = true;
        }
    }
}

uint32_t cpu_call_guest(cpu_t *cpu, uint32_t func_addr, int nargs, uint32_t const *args) {
    uint32_t saved_x[8];
    memcpy(saved_x, &cpu->x[10], sizeof(saved_x));
    uint32_t saved_ra = cpu->x[1];
    uint32_t saved_pc = cpu->pc;

    for (int i = 0; i < nargs && i < 8; i++) {
        cpu->x[10 + i] = args[i];
    }
    cpu->x[1] = CPU_CALL_SENTINEL;
    cpu->pc   = func_addr;
    cpu_run_until_return(cpu);

    uint32_t result = cpu->x[10];
    memcpy(&cpu->x[10], saved_x, sizeof(saved_x));
    cpu->x[1] = saved_ra;
    cpu->pc   = saved_pc;
    return result;
}
