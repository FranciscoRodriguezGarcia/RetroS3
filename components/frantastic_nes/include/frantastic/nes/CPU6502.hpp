#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>

namespace frantastic::nes {

// CPU-side bus contract. Every read() or write() is exactly one CPU cycle;
// the implementation advances the rest of the machine as a side effect, so
// instruction timing emerges from the documented bus-access sequence rather
// than from a per-opcode cycle table.
class CpuBus {
public:
    virtual ~CpuBus() = default;
    virtual std::uint8_t read(std::uint16_t address) = 0;
    virtual void write(std::uint16_t address, std::uint8_t value) = 0;
    // Side-effect-free read for disassembly/tracing.
    virtual std::uint8_t peek(std::uint16_t address) const = 0;
    // Edge-detected NMI latch (set on a rising edge of the PPU /NMI output).
    virtual bool nmiPending() const { return false; }
    virtual void acknowledgeNmi() {}
    // Level-sensitive IRQ line (APU frame counter, DMC, mapper).
    virtual bool irqAsserted() const { return false; }
};

namespace Flag {
constexpr std::uint8_t C = 0x01;
constexpr std::uint8_t Z = 0x02;
constexpr std::uint8_t I = 0x04;
constexpr std::uint8_t D = 0x08;
constexpr std::uint8_t B = 0x10;
constexpr std::uint8_t U = 0x20;
constexpr std::uint8_t V = 0x40;
constexpr std::uint8_t N = 0x80;
} // namespace Flag

enum class AddressingMode {
    Implied, Accumulator, Immediate, ZeroPage, ZeroPageX, ZeroPageY,
    Absolute, AbsoluteX, AbsoluteY, IndirectX, IndirectY, Indirect, Relative
};

// How an instruction uses its effective address; selects the bus sequence.
enum class AccessKind { Read, Write, ReadModifyWrite, Implied, Branch, Control, Unsupported };

class UnsupportedOpcodeError : public std::runtime_error {
public:
    UnsupportedOpcodeError(std::uint8_t opcode, std::uint16_t pc);
    std::uint8_t opcode() const { return opcode_; }
    std::uint16_t pc() const { return pc_; }
private:
    std::uint8_t opcode_;
    std::uint16_t pc_;
};

// Ricoh 2A03 CPU core: an NMOS 6502 without decimal mode. The D flag can be
// set and cleared, but ADC/SBC always perform binary arithmetic.
class CPU6502 {
    friend class NesStateCodec;
public:
    using ReadOp = void (CPU6502::*)(std::uint8_t);
    using WriteOp = std::uint8_t (CPU6502::*)();
    using ModifyOp = std::uint8_t (CPU6502::*)(std::uint8_t);
    using ImpliedOp = void (CPU6502::*)();
    using ControlOp = void (CPU6502::*)();

    // One row of the 256-entry decode table. Exactly one handler is set,
    // matching `kind`. `cycles` is the documented base cycle count, kept for
    // tests and disassembly; execution timing comes from bus accesses.
    struct Instruction {
        const char* mnemonic = "???";
        AddressingMode mode = AddressingMode::Implied;
        AccessKind kind = AccessKind::Unsupported;
        std::uint8_t cycles = 0;
        ReadOp read = nullptr;
        WriteOp write = nullptr;
        ModifyOp modify = nullptr;
        ImpliedOp implied = nullptr;
        ControlOp control = nullptr;
        std::uint8_t branchFlag = 0;   // Flag tested by a branch.
        bool branchIfSet = false;
    };
    static const std::array<Instruction, 256>& instructionTable();
    static unsigned instructionLength(AddressingMode mode);

    explicit CPU6502(CpuBus& bus);

    // Power-on clears A/X/Y, sets P=$34 and runs the reset sequence (S ends at $FD).
    void powerOn();
    // Reset sequence: 7 cycles, three suppressed stack pushes, I set, PC from $FFFC.
    void reset();
    // Executes one instruction, or one pending interrupt sequence. Returns
    // the CPU cycles consumed by the CPU's own bus accesses.
    unsigned step();

    std::uint16_t pc = 0;
    std::uint8_t a = 0, x = 0, y = 0, s = 0xFD, p = 0x34;

    std::uint64_t cycles() const { return cycles_; }
    void setCycles(std::uint64_t value) { cycles_ = value; }
    std::uint64_t instructionCount() const { return instructions_; }
    bool halted() const { return halted_; }
    bool flag(std::uint8_t mask) const { return (p & mask) != 0; }

    // Called before each instruction executes (not before interrupt entry).
    void setTraceHook(std::function<void(const CPU6502&)> hook) { trace_ = std::move(hook); }
    std::string disassemble(std::uint16_t address, unsigned* length = nullptr) const;
    // "PC  bytes  MNEMONIC operand  A:.. X:.. Y:.. P:.. SP:.. CYC:n"
    std::string traceLine() const;

private:
    struct InterruptPoll { bool nmi = false; bool irq = false; };

    std::uint8_t read(std::uint16_t address);
    void write(std::uint16_t address, std::uint8_t value);
    std::uint8_t fetch() { return read(pc++); }
    void push(std::uint8_t value);
    std::uint8_t pull();
    void poll();
    void setZN(std::uint8_t value);
    void compare(std::uint8_t reg, std::uint8_t value);
    void addWithCarry(std::uint8_t value);

    // Address resolution. `alwaysFix` forces the extra cycle used by writes
    // and read-modify-write instructions on indexed modes.
    std::uint16_t resolveAddress(AddressingMode mode, bool alwaysFix);
    void executeRead(const Instruction& in);
    void executeWrite(const Instruction& in);
    void executeModify(const Instruction& in);
    void executeImplied(const Instruction& in);
    void executeBranch(const Instruction& in);
    void interruptSequence(bool nmi);

    // Read operations.
    void opLDA(std::uint8_t v); void opLDX(std::uint8_t v); void opLDY(std::uint8_t v);
    void opADC(std::uint8_t v); void opSBC(std::uint8_t v);
    void opAND(std::uint8_t v); void opORA(std::uint8_t v); void opEOR(std::uint8_t v);
    void opCMP(std::uint8_t v); void opCPX(std::uint8_t v); void opCPY(std::uint8_t v);
    void opBIT(std::uint8_t v);
    void opLAX(std::uint8_t v); void opLAS(std::uint8_t v);
    void opANC(std::uint8_t v); void opALR(std::uint8_t v);
    void opARR(std::uint8_t v); void opAXS(std::uint8_t v);
    void opNOPRead(std::uint8_t v);
    void opXAA(std::uint8_t v); void opLXA(std::uint8_t v);
    // Write operations.
    std::uint8_t opSTA(); std::uint8_t opSTX(); std::uint8_t opSTY(); std::uint8_t opSAX();
    std::uint8_t opAHX(); std::uint8_t opTAS();
    std::uint8_t opSHX(); std::uint8_t opSHY();
    // Read-modify-write operations (also used for accumulator forms).
    std::uint8_t opASL(std::uint8_t v); std::uint8_t opLSR(std::uint8_t v);
    std::uint8_t opROL(std::uint8_t v); std::uint8_t opROR(std::uint8_t v);
    std::uint8_t opINC(std::uint8_t v); std::uint8_t opDEC(std::uint8_t v);
    std::uint8_t opSLO(std::uint8_t v); std::uint8_t opRLA(std::uint8_t v);
    std::uint8_t opSRE(std::uint8_t v); std::uint8_t opRRA(std::uint8_t v);
    std::uint8_t opDCP(std::uint8_t v); std::uint8_t opISC(std::uint8_t v);
    // Implied operations.
    void opCLC(); void opSEC(); void opCLI(); void opSEI(); void opCLD(); void opSED(); void opCLV();
    void opTAX(); void opTAY(); void opTXA(); void opTYA(); void opTSX(); void opTXS();
    void opINX(); void opINY(); void opDEX(); void opDEY(); void opNOP(); void opKIL();
    // Control-flow / stack instructions with bespoke bus sequences.
    void opJMPAbs(); void opJMPInd(); void opJSR(); void opRTS(); void opRTI(); void opBRK();
    void opPHA(); void opPHP(); void opPLA(); void opPLP();

    static std::array<Instruction, 256> buildTable();

    CpuBus& bus_;
    std::uint64_t cycles_ = 0;
    std::uint64_t instructions_ = 0;
    InterruptPoll pollNow_{};      // Sampled at the end of the latest cycle.
    InterruptPoll pollPrevious_{}; // Sampled at the end of the cycle before.
    bool nmiRequested_ = false;
    bool irqRequested_ = false;
    bool halted_ = false;
    std::function<void(const CPU6502&)> trace_;
};

} // namespace frantastic::nes
