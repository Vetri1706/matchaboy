#include "dmg/cpu.hpp"
#include "dmg/mmu.hpp"
#ifdef ENABLE_AUTOPSY
#include "dmg/autopsy.hpp"
#endif
#include <array>
#include <iomanip>
#include <sstream>
namespace dmg {
namespace {
struct StepGuard {
    bool &active;
    explicit StepGuard(bool &value) : active(value) { active = true; }
    ~StepGuard() { active = false; }
};
#ifdef ENABLE_AUTOPSY
struct AutopsyStep {
    Cpu &cpu;
    Bus &bus;
    std::uint64_t instructions, frames;
    AutopsyStep(Cpu &c, Bus &b) : cpu(c), bus(b), instructions(c.instructions), frames(b.ppu.frames()) {}
    ~AutopsyStep() {
        if (bus.autopsy != nullptr) {
            if (cpu.instructions != instructions)
                bus.autopsy->instruction(cpu, bus);
            if (bus.ppu.frames() != frames)
                bus.autopsy->frame(cpu, bus);
        }
    }
};
#endif
constexpr std::uint16_t join(std::uint8_t high, std::uint8_t low) {
    return static_cast<std::uint16_t>((static_cast<unsigned>(high) << 8U) | low);
}
constexpr std::uint16_t offset(std::uint16_t base, std::uint8_t displacement) {
    const int signed_displacement = displacement < 128 ? displacement : displacement - 256;
    return static_cast<std::uint16_t>(base + signed_displacement);
}
std::string hex(unsigned value, unsigned width) {
    std::ostringstream out;
    out << '$' << std::uppercase << std::hex << std::setfill('0')
        << std::setw(static_cast<int>(width)) << value;
    return out.str();
}
} // namespace
std::uint8_t Cpu::fetch8() {
    const auto value = bus_.read(pc, true);
    if (last_byte_count < last_bytes.size())
        last_bytes[last_byte_count++] = value;
    pc = static_cast<std::uint16_t>(pc + 1U);
    return value;
}
std::uint16_t Cpu::fetch16() {
    const auto low = fetch8();
    const auto high = fetch8();
    return join(high, low);
}
std::uint16_t Cpu::pair(unsigned index) const {
    switch (index) {
    case 0:
        return join(b, c);
    case 1:
        return join(d, e);
    case 2:
        return join(h, l);
    default:
        return sp;
    }
}
void Cpu::set_pair(unsigned index, std::uint16_t value) {
    const auto high = static_cast<std::uint8_t>(value >> 8U),
               low = static_cast<std::uint8_t>(value);
    switch (index) {
    case 0:
        b = high;
        c = low;
        break;
    case 1:
        d = high;
        e = low;
        break;
    case 2:
        h = high;
        l = low;
        break;
    default:
        sp = value;
        break;
    }
}
std::uint8_t Cpu::reg(unsigned index) {
    switch (index) {
    case 0:
        return b;
    case 1:
        return c;
    case 2:
        return d;
    case 3:
        return e;
    case 4:
        return h;
    case 5:
        return l;
    case 6:
        return bus_.read(pair(2));
    default:
        return a;
    }
}
void Cpu::set_reg(unsigned index, std::uint8_t value) {
    switch (index) {
    case 0:
        b = value;
        break;
    case 1:
        c = value;
        break;
    case 2:
        d = value;
        break;
    case 3:
        e = value;
        break;
    case 4:
        h = value;
        break;
    case 5:
        l = value;
        break;
    case 6:
        bus_.write(pair(2), value);
        break;
    default:
        a = value;
        break;
    }
}
bool Cpu::condition(unsigned index) const {
    switch (index) {
    case 0:
        return (f & Z) == 0;
    case 1:
        return (f & Z) != 0;
    case 2:
        return (f & C) == 0;
    default:
        return (f & C) != 0;
    }
}
void Cpu::push(std::uint16_t value) {
    sp = static_cast<std::uint16_t>(sp - 1U);
    bus_.write(sp, static_cast<std::uint8_t>(value >> 8U));
    sp = static_cast<std::uint16_t>(sp - 1U);
    bus_.write(sp, static_cast<std::uint8_t>(value));
}
std::uint16_t Cpu::pop() {
    const auto low = bus_.read(sp, true);
    sp = static_cast<std::uint16_t>(sp + 1U);
    const auto high = bus_.read(sp);
    sp = static_cast<std::uint16_t>(sp + 1U);
    return join(high, low);
}
void Cpu::alu(unsigned operation, std::uint8_t operand) {
    const unsigned lhs = a, rhs = operand;
    const unsigned carry = ((operation == 1 || operation == 3) && (f & C)) ? 1U : 0U;
    switch (operation) {
    case 0:
    case 1: {
        const unsigned result = lhs + rhs + carry;
        a = static_cast<std::uint8_t>(result);
        f = static_cast<std::uint8_t>((a == 0 ? Z : 0) |
                                      (((lhs & 15U) + (rhs & 15U) + carry > 15U) ? H : 0) |
                                      (result > 255U ? C : 0));
        break;
    }
    case 2:
    case 3:
    case 7: {
        const auto result = static_cast<std::uint8_t>(lhs - rhs - carry);
        f = static_cast<std::uint8_t>((result == 0 ? Z : 0) | N |
                                      ((lhs & 15U) < (rhs & 15U) + carry ? H : 0) |
                                      (lhs < rhs + carry ? C : 0));
        if (operation != 7)
            a = result;
        break;
    }
    case 4:
        a = static_cast<std::uint8_t>(a & operand);
        f = H;
        break;
    case 5:
        a = static_cast<std::uint8_t>(a ^ operand);
        f = 0;
        break;
    default:
        a = static_cast<std::uint8_t>(a | operand);
        f = 0;
        break;
    }
    if (operation >= 4 && operation <= 6 && a == 0)
        f |= Z;
}
std::uint8_t Cpu::rotate(unsigned operation, std::uint8_t value) {
    const unsigned incoming = (f & C) ? 1U : 0U;
    unsigned result = value, outgoing = 0;
    switch (operation) {
    case 0:
        outgoing = value >> 7U;
        result = (value << 1U) | outgoing;
        break;
    case 1:
        outgoing = value & 1U;
        result = (value >> 1U) | (outgoing << 7U);
        break;
    case 2:
        outgoing = value >> 7U;
        result = (value << 1U) | incoming;
        break;
    case 3:
        outgoing = value & 1U;
        result = (value >> 1U) | (incoming << 7U);
        break;
    case 4:
        outgoing = value >> 7U;
        result = value << 1U;
        break;
    case 5:
        outgoing = value & 1U;
        result = (value >> 1U) | (value & 0x80U);
        break;
    case 6:
        result = (value << 4U) | (value >> 4U);
        break;
    default:
        outgoing = value & 1U;
        result = value >> 1U;
        break;
    }
    const auto byte = static_cast<std::uint8_t>(result);
    f = static_cast<std::uint8_t>((byte == 0 ? Z : 0) | (outgoing ? C : 0));
    return byte;
}
void Cpu::daa() {
    unsigned correction = 0;
    bool carry = (f & C) != 0;
    if ((f & N) == 0) {
        if (carry || a > 0x99) {
            correction |= 0x60U;
            carry = true;
        }
        if ((f & H) || (a & 15U) > 9U)
            correction |= 6U;
        a = static_cast<std::uint8_t>(a + correction);
    } else {
        if (carry)
            correction |= 0x60U;
        if (f & H)
            correction |= 6U;
        a = static_cast<std::uint8_t>(a - correction);
    }
    f = static_cast<std::uint8_t>((f & N) | (a == 0 ? Z : 0) | (carry ? C : 0));
}
void Cpu::extended(std::uint8_t opcode) {
    const unsigned x = opcode >> 6U, y = (opcode >> 3U) & 7U, z = opcode & 7U;
    auto value = reg(z);
    if (x == 0) {
        value = rotate(y, value);
        set_reg(z, value);
    } else if (x == 1)
        f = static_cast<std::uint8_t>((f & C) | H | ((value & (1U << y)) ? 0 : Z));
    else if (x == 2)
        set_reg(z, static_cast<std::uint8_t>(value & ~(1U << y)));
    else
        set_reg(z, static_cast<std::uint8_t>(value | (1U << y)));
}
void Cpu::prefetch() {
    ir_address_ = pc;
    std::uint8_t requests = 0;
    ir_ = bus_.read(pc, !halt_bug_, &requests);
    next_pc_ = halt_bug_ ? pc : static_cast<std::uint16_t>(pc + 1U);
    halt_bug_ = false;
    interrupt_latched_ = ime && requests != 0;
    prefetched_ = true;
}
void Cpu::prime() {
    const StepGuard guard(step_active_);
    prefetched_ = false;
    interrupt_latched_ = false;
    halt_bug_ = false;
    enable_pending_ = false;
    prefetch();
}
void Cpu::service_interrupt() {
    ime = false;
    enable_pending_ = false;
    halted = false;
    interrupt_latched_ = false;
    // Discard the fetched IR by backing out the physical PC increment. This
    // IDU bus drive is observable as OAM corruption even without a memory read.
    bus_.idu_idle(next_pc_);
    pc = static_cast<std::uint16_t>(next_pc_ - 1U);
    bus_.idu_idle(sp);
    sp = static_cast<std::uint16_t>(sp - 1U);
    bus_.write(sp, static_cast<std::uint8_t>(pc >> 8U));
    // IE can be the high-byte stack target; select before the low-byte push.
    const auto pending = bus_.pending_interrupts();
    unsigned bit = 0;
    while (bit < 5 && (pending & (1U << bit)) == 0)
        ++bit;
    if (bit < 5)
        bus_.acknowledge_interrupt(bit);
    sp = static_cast<std::uint16_t>(sp - 1U);
    bus_.write(sp, static_cast<std::uint8_t>(pc));
    pc = bit < 5 ? static_cast<std::uint16_t>(0x40U + bit * 8U) : 0;
    prefetch();
}
void Cpu::step() {
    const StepGuard guard(step_active_);
#ifdef ENABLE_AUTOPSY
    const AutopsyStep telemetry(*this, bus_);
#endif
    if (locked) {
        bus_.idle();
        return;
    }
    if (stopped) {
        if (!bus_.joypad_wake())
            return;
        stopped = false;
    }
    if (halted) {
        const auto pending = bus_.pending_interrupts();
        if (!pending) {
            bus_.idle();
            return;
        }
        halted = false;
    }
    if (!prefetched_ || pc != ir_address_)
        prefetch();
    if (interrupt_latched_) {
        service_interrupt();
        return;
    }
    const bool enable_after_instruction = enable_pending_;
    enable_pending_ = false;
    const auto opcode = ir_;
    last_opcode = opcode;
    last_opcode_pc = ir_address_;
    last_bytes = {opcode, 0, 0};
    last_byte_count = 1;
    pc = next_pc_;
    prefetched_ = false;
    ++instructions;
    execute(opcode);
    if (enable_after_instruction && opcode != 0xF3)
        ime = true;
    f &= 0xF0;
    if (locked)
        bus_.idle();
    else if (stopped)
        prefetched_ = false;
    else if (halted) {
        // HALT drives PC once without an IDU increment before clock gating.
        // This dummy data is not executable IR; wake performs a fresh fetch.
        static_cast<void>(bus_.read(pc));
        prefetched_ = false;
    } else
        prefetch();
}
void Cpu::execute(std::uint8_t opcode) {
    const unsigned x = opcode >> 6U, y = (opcode >> 3U) & 7U, z = opcode & 7U, p = y >> 1U;
    const bool q = (y & 1U) != 0;
    if (x == 0) {
        switch (z) {
        case 0:
            if (y == 0)
                return;
            if (y == 1) {
                const auto address = fetch16();
                bus_.write(address, static_cast<std::uint8_t>(sp));
                bus_.write(static_cast<std::uint16_t>(address + 1U),
                           static_cast<std::uint8_t>(sp >> 8U));
            } else if (y == 2) {
                // STOP performs a non-incrementing padding-byte read before
                // shutting down the oscillator. Do not prefetch running code
                // once stopped; a joypad wake will perform a fresh fetch.
                const auto padding = bus_.read(pc);
                last_bytes[1] = padding;
                last_byte_count = 2;
                pc = static_cast<std::uint16_t>(pc + 1U);
                bus_.reset_divider();
                stopped = true;
            } else {
                const auto displacement = fetch8();
                if (y == 3 || condition(y - 4U)) {
                    bus_.idle();
                    pc = offset(pc, displacement);
                }
            }
            return;
        case 1:
            if (!q)
                set_pair(p, fetch16());
            else {
                const unsigned lhs = pair(2), rhs = pair(p);
                bus_.idle();
                f = static_cast<std::uint8_t>((f & Z) |
                                              (((lhs & 0xFFFU) + (rhs & 0xFFFU) > 0xFFFU) ? H : 0) |
                                              (lhs + rhs > 0xFFFFU ? C : 0));
                set_pair(2, static_cast<std::uint16_t>(lhs + rhs));
            }
            return;
        case 2: {
            const auto address = pair(p < 2 ? p : 2);
            if (q)
                a = bus_.read(address, p >= 2);
            else
                bus_.write(address, a);
            if (p == 2)
                set_pair(2, static_cast<std::uint16_t>(address + 1U));
            if (p == 3)
                set_pair(2, static_cast<std::uint16_t>(address - 1U));
            return;
        }
        case 3:
            bus_.idu_idle(pair(p));
            set_pair(p, static_cast<std::uint16_t>(q ? pair(p) - 1U : pair(p) + 1U));
            return;
        case 4: {
            const auto before = reg(y), after = static_cast<std::uint8_t>(before + 1U);
            f = static_cast<std::uint8_t>((f & C) | (after == 0 ? Z : 0) |
                                          ((before & 15U) == 15U ? H : 0));
            set_reg(y, after);
            return;
        }
        case 5: {
            const auto before = reg(y), after = static_cast<std::uint8_t>(before - 1U);
            f = static_cast<std::uint8_t>((f & C) | N | (after == 0 ? Z : 0) |
                                          ((before & 15U) == 0 ? H : 0));
            set_reg(y, after);
            return;
        }
        case 6:
            set_reg(y, fetch8());
            return;
        default:
            if (y < 4) {
                a = rotate(y, a);
                f &= static_cast<std::uint8_t>(~Z);
            } else if (y == 4)
                daa();
            else if (y == 5) {
                a = static_cast<std::uint8_t>(~a);
                f |= N | H;
            } else if (y == 6)
                f = static_cast<std::uint8_t>((f & Z) | C);
            else
                f = static_cast<std::uint8_t>((f & Z) | ((f & C) ? 0 : C));
            return;
        }
    }
    if (x == 1) {
        if (opcode == 0x76) {
            if (!ime && bus_.pending_interrupts())
                halt_bug_ = true;
            else
                halted = true;
        } else
            set_reg(y, reg(z));
        return;
    }
    if (x == 2) {
        alu(y, reg(z));
        return;
    }
    switch (z) {
    case 0:
        if (y < 4) {
            bus_.idle();
            if (condition(y)) {
                pc = pop();
                bus_.idle();
            }
        } else if (y == 4) {
            const auto address = static_cast<std::uint16_t>(0xFF00U + fetch8());
            bus_.write(address, a);
        } else if (y == 6) {
            const auto address = static_cast<std::uint16_t>(0xFF00U + fetch8());
            a = bus_.read(address);
        } else {
            const auto displacement = fetch8();
            const auto result = offset(sp, displacement);
            f = static_cast<std::uint8_t>(((sp & 15U) + (displacement & 15U) > 15U ? H : 0) |
                                          ((sp & 255U) + displacement > 255U ? C : 0));
            bus_.idle();
            if (y == 5) {
                bus_.idle();
                sp = result;
            } else
                set_pair(2, result);
        }
        return;
    case 1:
        if (!q) {
            const auto value = pop();
            if (p == 3) {
                a = static_cast<std::uint8_t>(value >> 8U);
                f = static_cast<std::uint8_t>(value & 0xF0U);
            } else
                set_pair(p, value);
        } else if (p < 2) {
            pc = pop();
            bus_.idle();
            if (p == 1) {
                ime = true;
                enable_pending_ = false;
            }
        } else if (p == 2)
            pc = pair(2);
        else {
            bus_.idle();
            sp = pair(2);
        }
        return;
    case 2:
        if (y < 4) {
            const auto address = fetch16();
            if (condition(y)) {
                bus_.idle();
                pc = address;
            }
        } else if (y == 4)
            bus_.write(static_cast<std::uint16_t>(0xFF00U + c), a);
        else if (y == 5) {
            const auto address = fetch16();
            bus_.write(address, a);
        } else if (y == 6)
            a = bus_.read(static_cast<std::uint16_t>(0xFF00U + c));
        else {
            const auto address = fetch16();
            a = bus_.read(address);
        }
        return;
    case 3:
        if (y == 0) {
            const auto address = fetch16();
            bus_.idle();
            pc = address;
        } else if (y == 1)
            extended(fetch8());
        else if (y == 6) {
            ime = false;
            enable_pending_ = false;
        } else if (y == 7)
            enable_pending_ = true;
        else
            locked = true;
        return;
    case 4:
        if (y < 4) {
            const auto address = fetch16();
            if (condition(y)) {
                bus_.idu_idle(sp);
                push(pc);
                pc = address;
            }
        } else
            locked = true;
        return;
    case 5:
        if (!q) {
            const auto value = p == 3 ? join(a, f) : pair(p);
            bus_.idu_idle(sp);
            push(value);
        } else if (p == 0) {
            const auto address = fetch16();
            bus_.idu_idle(sp);
            push(pc);
            pc = address;
        } else
            locked = true;
        return;
    case 6:
        alu(y, fetch8());
        return;
    default:
        bus_.idu_idle(sp);
        push(pc);
        pc = static_cast<std::uint16_t>(y * 8U);
        return;
    }
}
std::string Cpu::disassemble(std::uint16_t address, std::uint8_t opcode, std::uint8_t arg1,
                             std::uint8_t arg2) {
    static constexpr std::array<const char *, 8> r = {"B", "C", "D", "E", "H", "L", "(HL)", "A"};
    static constexpr std::array<const char *, 4> rp = {"BC", "DE", "HL", "SP"},
                                                 stk = {"BC", "DE", "HL", "AF"},
                                                 mem = {"(BC)", "(DE)", "(HL+)", "(HL-)"},
                                                 cc = {"NZ", "Z", "NC", "C"};
    static constexpr std::array<const char *, 8> ops = {"ADD A,", "ADC A,", "SUB ", "SBC A,",
                                                        "AND ",   "XOR ",   "OR ",  "CP "},
                                                 misc = {"RLCA", "RRCA", "RLA", "RRA",
                                                         "DAA",  "CPL",  "SCF", "CCF"},
                                                 rot = {"RLC ", "RRC ", "RL ",   "RR ",
                                                        "SLA ", "SRA ", "SWAP ", "SRL "};
    const unsigned x = opcode >> 6U, y = (opcode >> 3U) & 7U, z = opcode & 7U, p = y >> 1U;
    const bool q = (y & 1U) != 0;
    const auto immediate = hex(join(arg2, arg1), 4),
               relative = hex(offset(static_cast<std::uint16_t>(address + 2U), arg1), 4),
               signed_arg = std::to_string(arg1 < 128 ? static_cast<int>(arg1)
                                                      : static_cast<int>(arg1) - 256);
    if (x == 0) {
        switch (z) {
        case 0:
            if (y == 0)
                return "NOP";
            if (y == 1)
                return "LD (" + immediate + "),SP";
            if (y == 2)
                return "STOP " + hex(arg1, 2);
            if (y == 3)
                return "JR " + relative;
            return std::string("JR ") + cc[y - 4U] + ',' + relative;
        case 1:
            return q ? std::string("ADD HL,") + rp[p]
                     : std::string("LD ") + rp[p] + ',' + immediate;
        case 2:
            return q ? std::string("LD A,") + mem[p] : std::string("LD ") + mem[p] + ",A";
        case 3:
            return std::string(q ? "DEC " : "INC ") + rp[p];
        case 4:
            return std::string("INC ") + r[y];
        case 5:
            return std::string("DEC ") + r[y];
        case 6:
            return std::string("LD ") + r[y] + ',' + hex(arg1, 2);
        default:
            return misc[y];
        }
    }
    if (x == 1)
        return opcode == 0x76 ? "HALT" : std::string("LD ") + r[y] + ',' + r[z];
    if (x == 2)
        return std::string(ops[y]) + r[z];
    switch (z) {
    case 0:
        if (y < 4)
            return std::string("RET ") + cc[y];
        if (y == 4)
            return "LDH (" + hex(0xFF00U + arg1, 4) + "),A";
        if (y == 5)
            return "ADD SP," + signed_arg;
        if (y == 6)
            return "LDH A,(" + hex(0xFF00U + arg1, 4) + ')';
        return "LD HL,SP" + std::string(arg1 < 128 ? "+" : "") + signed_arg;
    case 1:
        if (!q)
            return std::string("POP ") + stk[p];
        if (p == 0)
            return "RET";
        if (p == 1)
            return "RETI";
        return p == 2 ? "JP HL" : "LD SP,HL";
    case 2:
        if (y < 4)
            return std::string("JP ") + cc[y] + ',' + immediate;
        if (y == 4)
            return "LDH (C),A";
        if (y == 5)
            return "LD (" + immediate + "),A";
        if (y == 6)
            return "LDH A,(C)";
        return "LD A,(" + immediate + ')';
    case 3:
        if (y == 0)
            return "JP " + immediate;
        if (y == 1) {
            const unsigned bx = arg1 >> 6U, by = (arg1 >> 3U) & 7U, bz = arg1 & 7U;
            if (bx == 0)
                return std::string(rot[by]) + r[bz];
            return std::string(bx == 1   ? "BIT "
                               : bx == 2 ? "RES "
                                         : "SET ") +
                   std::to_string(by) + ',' + r[bz];
        }
        if (y == 6)
            return "DI";
        if (y == 7)
            return "EI";
        break;
    case 4:
        if (y < 4)
            return std::string("CALL ") + cc[y] + ',' + immediate;
        break;
    case 5:
        if (!q)
            return std::string("PUSH ") + stk[p];
        if (p == 0)
            return "CALL " + immediate;
        break;
    case 6:
        return std::string(ops[y]) + hex(arg1, 2);
    default:
        return "RST " + hex(y * 8U, 2);
    }
    return "LOCK " + hex(opcode, 2);
}
std::string Cpu::describe() const {
    const bool has_ir = prefetched_ && pc == ir_address_;
    const auto op = has_ir ? ir_ : bus_.peek(pc);
    const auto operand = has_ir ? next_pc_ : static_cast<std::uint16_t>(pc + 1U);
    const auto n1 = bus_.peek(operand), n2 = bus_.peek(static_cast<std::uint16_t>(operand + 1U));
    std::ostringstream out;
    out << "T=" << bus_.cycles() << " PC=" << hex(pc, 4) << " SP=" << hex(sp, 4)
        << " A=" << hex(a, 2) << " F=" << hex(f, 2) << " BC=" << hex(join(b, c), 4)
        << " DE=" << hex(join(d, e), 4) << " HL=" << hex(join(h, l), 4) << " IME=" << ime
        << " HALT=" << halted << " STOP=" << stopped << " LOCK=" << locked << " [" << hex(op, 2)
        << ' ' << hex(n1, 2) << ' ' << hex(n2, 2) << "] " << disassemble(pc, op, n1, n2);
    return out.str();
}
} // namespace dmg
