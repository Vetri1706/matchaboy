#pragma once
#include <array>
#include <cstdint>
#include <string>
namespace dmg {
class Bus;
class Cpu {
  public:
    explicit Cpu(Bus &bus) : bus_(bus) {}
    std::uint8_t a = 0x01, f = 0xb0;
    std::uint8_t b = 0x00, c = 0x13, d = 0x00, e = 0xd8;
    std::uint8_t h = 0x01, l = 0x4d;
    std::uint16_t sp = 0xfffe, pc = 0x0100;
    bool ime = false, halted = false, stopped = false, locked = false;
    std::uint64_t instructions = 0;
    // Prime the hardware instruction register after debugger code/register edits.
    // The fetch consumes a real M-cycle, independently of subsequent execution.
    void prime();
    std::uint8_t last_opcode = 0;
    std::uint16_t last_opcode_pc = 0;
    std::array<std::uint8_t, 3> last_bytes{};
    unsigned last_byte_count = 0;
    void step();
    [[nodiscard]] std::string describe() const;
    [[nodiscard]] static std::string disassemble(std::uint16_t address, std::uint8_t opcode,
                                                 std::uint8_t arg1, std::uint8_t arg2);

  private:
    friend struct SnapshotAccess;
    bool step_active_{};
    static constexpr std::uint8_t Z = 0x80, N = 0x40, H = 0x20, C = 0x10;
    Bus &bus_;
    bool halt_bug_ = false;
    bool enable_pending_ = false;
    bool prefetched_ = false, interrupt_latched_ = false;
    std::uint8_t ir_ = 0;
    std::uint16_t ir_address_ = 0, next_pc_ = 0;
    void prefetch();
    [[nodiscard]] std::uint8_t fetch8();
    [[nodiscard]] std::uint16_t fetch16();
    [[nodiscard]] std::uint16_t pair(unsigned index) const;
    void set_pair(unsigned index, std::uint16_t value);
    [[nodiscard]] std::uint8_t reg(unsigned index);
    void set_reg(unsigned index, std::uint8_t value);
    [[nodiscard]] bool condition(unsigned index) const;
    void push(std::uint16_t value);
    [[nodiscard]] std::uint16_t pop();
    void alu(unsigned operation, std::uint8_t operand);
    [[nodiscard]] std::uint8_t rotate(unsigned operation, std::uint8_t value);
    void daa();
    void extended(std::uint8_t opcode);
    void execute(std::uint8_t opcode);
    void service_interrupt();
};
} // namespace dmg
