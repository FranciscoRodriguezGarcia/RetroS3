#include "frantastic/nes/CPU6502.hpp"
#include <cstdio>
#include <initializer_list>
#include <string>

namespace frantastic::nes {

UnsupportedOpcodeError::UnsupportedOpcodeError(std::uint8_t opcode, std::uint16_t pc)
    : std::runtime_error([&] {
          char text[96];
          std::snprintf(text, sizeof text,
                        "NES CPU: unsupported (undocumented) opcode $%02X at $%04X", opcode, pc);
          return std::string(text);
      }()),
      opcode_(opcode), pc_(pc) {}

namespace {
using M = AddressingMode;
} // namespace

unsigned CPU6502::instructionLength(AddressingMode mode) {
    switch (mode) {
    case M::Implied: case M::Accumulator: return 1;
    case M::Absolute: case M::AbsoluteX: case M::AbsoluteY: case M::Indirect: return 3;
    default: return 2;
    }
}

std::array<CPU6502::Instruction, 256> CPU6502::buildTable() {
    std::array<Instruction, 256> t{};
    auto rd = [&](std::uint8_t op, const char* name, M mode, std::uint8_t cyc, ReadOp fn) {
        Instruction& i = t[op];
        i.mnemonic = name; i.mode = mode; i.kind = AccessKind::Read; i.cycles = cyc; i.read = fn;
    };
    auto wr = [&](std::uint8_t op, const char* name, M mode, std::uint8_t cyc, WriteOp fn) {
        Instruction& i = t[op];
        i.mnemonic = name; i.mode = mode; i.kind = AccessKind::Write; i.cycles = cyc; i.write = fn;
    };
    auto rmw = [&](std::uint8_t op, const char* name, M mode, std::uint8_t cyc, ModifyOp fn) {
        Instruction& i = t[op];
        i.mnemonic = name; i.mode = mode; i.kind = AccessKind::ReadModifyWrite; i.cycles = cyc; i.modify = fn;
    };
    auto imp = [&](std::uint8_t op, const char* name, ImpliedOp fn) {
        Instruction& i = t[op];
        i.mnemonic = name; i.mode = M::Implied; i.kind = AccessKind::Implied; i.cycles = 2; i.implied = fn;
    };
    auto br = [&](std::uint8_t op, const char* name, std::uint8_t flag, bool set) {
        Instruction& i = t[op];
        i.mnemonic = name; i.mode = M::Relative; i.kind = AccessKind::Branch; i.cycles = 2;
        i.branchFlag = flag; i.branchIfSet = set;
    };
    auto ctl = [&](std::uint8_t op, const char* name, M mode, std::uint8_t cyc, ControlOp fn) {
        Instruction& i = t[op];
        i.mnemonic = name; i.mode = mode; i.kind = AccessKind::Control; i.cycles = cyc; i.control = fn;
    };
    // The eight-mode ALU group shares a regular encoding: aaa bbb 01.
    struct Alu { std::uint8_t base; const char* name; ReadOp fn; };
    const Alu alu[] = {
        {0x00, "ORA", &CPU6502::opORA}, {0x20, "AND", &CPU6502::opAND},
        {0x40, "EOR", &CPU6502::opEOR}, {0x60, "ADC", &CPU6502::opADC},
        {0xA0, "LDA", &CPU6502::opLDA}, {0xC0, "CMP", &CPU6502::opCMP},
        {0xE0, "SBC", &CPU6502::opSBC},
    };
    for (const auto& g : alu) {
        rd(g.base | 0x09, g.name, M::Immediate, 2, g.fn);
        rd(g.base | 0x05, g.name, M::ZeroPage, 3, g.fn);
        rd(g.base | 0x15, g.name, M::ZeroPageX, 4, g.fn);
        rd(g.base | 0x0D, g.name, M::Absolute, 4, g.fn);
        rd(g.base | 0x1D, g.name, M::AbsoluteX, 4, g.fn);
        rd(g.base | 0x19, g.name, M::AbsoluteY, 4, g.fn);
        rd(g.base | 0x01, g.name, M::IndirectX, 6, g.fn);
        rd(g.base | 0x11, g.name, M::IndirectY, 5, g.fn);
    }
    // STA has no immediate form.
    wr(0x85, "STA", M::ZeroPage, 3, &CPU6502::opSTA);
    wr(0x95, "STA", M::ZeroPageX, 4, &CPU6502::opSTA);
    wr(0x8D, "STA", M::Absolute, 4, &CPU6502::opSTA);
    wr(0x9D, "STA", M::AbsoluteX, 5, &CPU6502::opSTA);
    wr(0x99, "STA", M::AbsoluteY, 5, &CPU6502::opSTA);
    wr(0x81, "STA", M::IndirectX, 6, &CPU6502::opSTA);
    wr(0x91, "STA", M::IndirectY, 6, &CPU6502::opSTA);
    wr(0x86, "STX", M::ZeroPage, 3, &CPU6502::opSTX);
    wr(0x96, "STX", M::ZeroPageY, 4, &CPU6502::opSTX);
    wr(0x8E, "STX", M::Absolute, 4, &CPU6502::opSTX);
    wr(0x84, "STY", M::ZeroPage, 3, &CPU6502::opSTY);
    wr(0x94, "STY", M::ZeroPageX, 4, &CPU6502::opSTY);
    wr(0x8C, "STY", M::Absolute, 4, &CPU6502::opSTY);

    rd(0xA2, "LDX", M::Immediate, 2, &CPU6502::opLDX);
    rd(0xA6, "LDX", M::ZeroPage, 3, &CPU6502::opLDX);
    rd(0xB6, "LDX", M::ZeroPageY, 4, &CPU6502::opLDX);
    rd(0xAE, "LDX", M::Absolute, 4, &CPU6502::opLDX);
    rd(0xBE, "LDX", M::AbsoluteY, 4, &CPU6502::opLDX);
    rd(0xA0, "LDY", M::Immediate, 2, &CPU6502::opLDY);
    rd(0xA4, "LDY", M::ZeroPage, 3, &CPU6502::opLDY);
    rd(0xB4, "LDY", M::ZeroPageX, 4, &CPU6502::opLDY);
    rd(0xAC, "LDY", M::Absolute, 4, &CPU6502::opLDY);
    rd(0xBC, "LDY", M::AbsoluteX, 4, &CPU6502::opLDY);
    rd(0xE0, "CPX", M::Immediate, 2, &CPU6502::opCPX);
    rd(0xE4, "CPX", M::ZeroPage, 3, &CPU6502::opCPX);
    rd(0xEC, "CPX", M::Absolute, 4, &CPU6502::opCPX);
    rd(0xC0, "CPY", M::Immediate, 2, &CPU6502::opCPY);
    rd(0xC4, "CPY", M::ZeroPage, 3, &CPU6502::opCPY);
    rd(0xCC, "CPY", M::Absolute, 4, &CPU6502::opCPY);
    rd(0x24, "BIT", M::ZeroPage, 3, &CPU6502::opBIT);
    rd(0x2C, "BIT", M::Absolute, 4, &CPU6502::opBIT);

    // Shift/rotate/increment group: aaa bbb 10 with accumulator at bbb=010.
    struct Shift { std::uint8_t base; const char* name; ModifyOp fn; bool accumulator; };
    const Shift shifts[] = {
        {0x00, "ASL", &CPU6502::opASL, true}, {0x20, "ROL", &CPU6502::opROL, true},
        {0x40, "LSR", &CPU6502::opLSR, true}, {0x60, "ROR", &CPU6502::opROR, true},
        {0xC0, "DEC", &CPU6502::opDEC, false}, {0xE0, "INC", &CPU6502::opINC, false},
    };
    for (const auto& g : shifts) {
        if (g.accumulator) rmw(g.base | 0x0A, g.name, M::Accumulator, 2, g.fn);
        rmw(g.base | 0x06, g.name, M::ZeroPage, 5, g.fn);
        rmw(g.base | 0x16, g.name, M::ZeroPageX, 6, g.fn);
        rmw(g.base | 0x0E, g.name, M::Absolute, 6, g.fn);
        rmw(g.base | 0x1E, g.name, M::AbsoluteX, 7, g.fn);
    }

    imp(0x18, "CLC", &CPU6502::opCLC); imp(0x38, "SEC", &CPU6502::opSEC);
    imp(0x58, "CLI", &CPU6502::opCLI); imp(0x78, "SEI", &CPU6502::opSEI);
    imp(0xD8, "CLD", &CPU6502::opCLD); imp(0xF8, "SED", &CPU6502::opSED);
    imp(0xB8, "CLV", &CPU6502::opCLV);
    imp(0xAA, "TAX", &CPU6502::opTAX); imp(0xA8, "TAY", &CPU6502::opTAY);
    imp(0x8A, "TXA", &CPU6502::opTXA); imp(0x98, "TYA", &CPU6502::opTYA);
    imp(0xBA, "TSX", &CPU6502::opTSX); imp(0x9A, "TXS", &CPU6502::opTXS);
    imp(0xE8, "INX", &CPU6502::opINX); imp(0xC8, "INY", &CPU6502::opINY);
    imp(0xCA, "DEX", &CPU6502::opDEX); imp(0x88, "DEY", &CPU6502::opDEY);
    imp(0xEA, "NOP", &CPU6502::opNOP);

    br(0x10, "BPL", Flag::N, false); br(0x30, "BMI", Flag::N, true);
    br(0x50, "BVC", Flag::V, false); br(0x70, "BVS", Flag::V, true);
    br(0x90, "BCC", Flag::C, false); br(0xB0, "BCS", Flag::C, true);
    br(0xD0, "BNE", Flag::Z, false); br(0xF0, "BEQ", Flag::Z, true);


    // Stable NMOS 6502 undocumented instructions. Each instruction uses the
    // same bus-access sequence as its documented read/write/RMW counterpart.
    // See NESdev "Programming with unofficial opcodes".
    struct Combo { std::uint8_t base; const char* name; ModifyOp fn; };
    const Combo combos[] = {
        {0x00, "SLO", &CPU6502::opSLO}, {0x20, "RLA", &CPU6502::opRLA},
        {0x40, "SRE", &CPU6502::opSRE}, {0x60, "RRA", &CPU6502::opRRA},
        {0xC0, "DCP", &CPU6502::opDCP}, {0xE0, "ISC", &CPU6502::opISC},
    };
    for (const auto& g : combos) {
        rmw(g.base | 0x03, g.name, M::IndirectX, 8, g.fn);
        rmw(g.base | 0x07, g.name, M::ZeroPage, 5, g.fn);
        rmw(g.base | 0x0F, g.name, M::Absolute, 6, g.fn);
        rmw(g.base | 0x13, g.name, M::IndirectY, 8, g.fn);
        rmw(g.base | 0x17, g.name, M::ZeroPageX, 6, g.fn);
        rmw(g.base | 0x1B, g.name, M::AbsoluteY, 7, g.fn);
        rmw(g.base | 0x1F, g.name, M::AbsoluteX, 7, g.fn);
    }
    rd(0xA3, "LAX", M::IndirectX, 6, &CPU6502::opLAX);
    rd(0xA7, "LAX", M::ZeroPage, 3, &CPU6502::opLAX);
    rd(0xAF, "LAX", M::Absolute, 4, &CPU6502::opLAX);
    rd(0xB3, "LAX", M::IndirectY, 5, &CPU6502::opLAX);
    rd(0xB7, "LAX", M::ZeroPageY, 4, &CPU6502::opLAX);
    rd(0xBF, "LAX", M::AbsoluteY, 4, &CPU6502::opLAX);
    wr(0x83, "SAX", M::IndirectX, 6, &CPU6502::opSAX);
    wr(0x87, "SAX", M::ZeroPage, 3, &CPU6502::opSAX);
    wr(0x8F, "SAX", M::Absolute, 4, &CPU6502::opSAX);
    wr(0x97, "SAX", M::ZeroPageY, 4, &CPU6502::opSAX);
    rd(0x0B, "ANC", M::Immediate, 2, &CPU6502::opANC);
    rd(0x2B, "ANC", M::Immediate, 2, &CPU6502::opANC);
    rd(0x4B, "ALR", M::Immediate, 2, &CPU6502::opALR);
    rd(0x6B, "ARR", M::Immediate, 2, &CPU6502::opARR);
    rd(0xCB, "AXS", M::Immediate, 2, &CPU6502::opAXS);
    rd(0xEB, "SBC", M::Immediate, 2, &CPU6502::opSBC);
    rd(0xBB, "LAS", M::AbsoluteY, 4, &CPU6502::opLAS);

    // NOP variants still perform their documented memory reads, including
    // read-side effects and the optional indexed page-crossing cycle.
    for (const auto op : {0x1Au, 0x3Au, 0x5Au, 0x7Au, 0xDAu, 0xFAu})
        imp(static_cast<std::uint8_t>(op), "NOP", &CPU6502::opNOP);
    for (const auto op : {0x80u, 0x82u, 0x89u, 0xC2u, 0xE2u})
        rd(static_cast<std::uint8_t>(op), "NOP", M::Immediate, 2, &CPU6502::opNOPRead);
    for (const auto op : {0x04u, 0x44u, 0x64u})
        rd(static_cast<std::uint8_t>(op), "NOP", M::ZeroPage, 3, &CPU6502::opNOPRead);
    for (const auto op : {0x14u, 0x34u, 0x54u, 0x74u, 0xD4u, 0xF4u})
        rd(static_cast<std::uint8_t>(op), "NOP", M::ZeroPageX, 4, &CPU6502::opNOPRead);
    rd(0x0C, "NOP", M::Absolute, 4, &CPU6502::opNOPRead);
    for (const auto op : {0x1Cu, 0x3Cu, 0x5Cu, 0x7Cu, 0xDCu, 0xFCu})
        rd(static_cast<std::uint8_t>(op), "NOP", M::AbsoluteX, 4, &CPU6502::opNOPRead);

    // JAM/KIL locks the CPU until reset, ignoring even NMI and IRQ.
    for (const auto op : {0x02u, 0x12u, 0x22u, 0x32u, 0x42u, 0x52u,
                          0x62u, 0x72u, 0x92u, 0xB2u, 0xD2u, 0xF2u})
        ctl(static_cast<std::uint8_t>(op), "KIL", M::Implied, 2, &CPU6502::opKIL);

    // Unstable NMOS opcodes: use the documented common 2A03/NMOS model.
    // XAA/LXA's analog "magic" input and high-byte store glitches can vary
    // by silicon/temperature; these approximations are NOT chip-exact.
    rd(0x8B, "XAA", M::Immediate, 2, &CPU6502::opXAA);
    rd(0xAB, "LXA", M::Immediate, 2, &CPU6502::opLXA);
    wr(0x93, "AHX", M::IndirectY, 6, &CPU6502::opAHX);
    wr(0x9F, "AHX", M::AbsoluteY, 5, &CPU6502::opAHX);
    wr(0x9B, "TAS", M::AbsoluteY, 5, &CPU6502::opTAS);
    wr(0x9C, "SHY", M::AbsoluteX, 5, &CPU6502::opSHY);
    wr(0x9E, "SHX", M::AbsoluteY, 5, &CPU6502::opSHX);

    ctl(0x4C, "JMP", M::Absolute, 3, &CPU6502::opJMPAbs);
    ctl(0x6C, "JMP", M::Indirect, 5, &CPU6502::opJMPInd);
    ctl(0x20, "JSR", M::Absolute, 6, &CPU6502::opJSR);
    ctl(0x60, "RTS", M::Implied, 6, &CPU6502::opRTS);
    ctl(0x40, "RTI", M::Implied, 6, &CPU6502::opRTI);
    ctl(0x00, "BRK", M::Implied, 7, &CPU6502::opBRK);
    ctl(0x48, "PHA", M::Implied, 3, &CPU6502::opPHA);
    ctl(0x08, "PHP", M::Implied, 3, &CPU6502::opPHP);
    ctl(0x68, "PLA", M::Implied, 4, &CPU6502::opPLA);
    ctl(0x28, "PLP", M::Implied, 4, &CPU6502::opPLP);
    return t;
}

const std::array<CPU6502::Instruction, 256>& CPU6502::instructionTable() {
    static const std::array<Instruction, 256> table = buildTable();
    return table;
}

CPU6502::CPU6502(CpuBus& bus) : bus_(bus) {}

std::uint8_t CPU6502::read(std::uint16_t address) {
    const std::uint8_t value = bus_.read(address);
    ++cycles_;
    poll();
    return value;
}

void CPU6502::write(std::uint16_t address, std::uint8_t value) {
    bus_.write(address, value);
    ++cycles_;
    poll();
}

// The 6502 samples its interrupt inputs during every cycle, but the decision
// to take an interrupt is made from the value sampled at the end of the
// penultimate cycle of an instruction. Keeping the last two samples lets the
// instruction boundary use the correct one.
void CPU6502::poll() {
    pollPrevious_ = pollNow_;
    pollNow_.nmi = bus_.nmiPending();
    pollNow_.irq = bus_.irqAsserted() && !(p & Flag::I);
}

void CPU6502::push(std::uint8_t value) {
    write(static_cast<std::uint16_t>(0x0100u | s), value);
    --s;
}

std::uint8_t CPU6502::pull() {
    ++s;
    return read(static_cast<std::uint16_t>(0x0100u | s));
}

void CPU6502::setZN(std::uint8_t v) {
    p = static_cast<std::uint8_t>((p & ~(Flag::Z | Flag::N)) | (v & Flag::N) | (v == 0 ? Flag::Z : 0));
}

void CPU6502::powerOn() {
    a = x = y = 0;
    s = 0x00;
    p = 0x34;
    reset();
}

void CPU6502::reset() {
    // Reset reuses the interrupt microcode with the stack writes turned into
    // reads, so S still drops by three.
    read(pc);
    read(pc);
    read(static_cast<std::uint16_t>(0x0100u | s)); --s;
    read(static_cast<std::uint16_t>(0x0100u | s)); --s;
    read(static_cast<std::uint16_t>(0x0100u | s)); --s;
    p |= Flag::I;
    halted_ = false;
    const std::uint8_t lo = read(0xFFFC);
    const std::uint8_t hi = read(0xFFFD);
    pc = static_cast<std::uint16_t>(lo | (hi << 8));
    nmiRequested_ = irqRequested_ = false;
    pollNow_ = pollPrevious_ = InterruptPoll{};
}

void CPU6502::interruptSequence(bool nmi) {
    read(pc);
    read(pc);
    push(static_cast<std::uint8_t>(pc >> 8));
    push(static_cast<std::uint8_t>(pc));
    // NMI hijacking: an NMI that becomes pending while the IRQ sequence is
    // pushing the return address takes over the vector fetch.
    if (!nmi && bus_.nmiPending()) nmi = true;
    push(static_cast<std::uint8_t>((p | Flag::U) & ~Flag::B));
    p |= Flag::I;
    const std::uint16_t vector = nmi ? 0xFFFA : 0xFFFE;
    if (nmi) bus_.acknowledgeNmi();
    const std::uint8_t lo = read(vector);
    const std::uint8_t hi = read(static_cast<std::uint16_t>(vector + 1));
    pc = static_cast<std::uint16_t>(lo | (hi << 8));
    // The instruction after the handler entry always executes before another
    // interrupt is recognised.
    pollNow_ = pollPrevious_ = InterruptPoll{};
}

unsigned CPU6502::step() {
    const std::uint64_t start = cycles_;
    if (halted_) {
        // The CPU has no instruction boundary after KIL; keep the rest of
        // the system clocking while the processor is stuck on its bus read.
        read(pc);
        return static_cast<unsigned>(cycles_ - start);
    }
    if (nmiRequested_ || irqRequested_) {
        const bool nmi = nmiRequested_;
        nmiRequested_ = irqRequested_ = false;
        interruptSequence(nmi);
        return static_cast<unsigned>(cycles_ - start);
    }
    if (trace_) trace_(*this);
    const std::uint16_t opcodePc = pc;
    const std::uint8_t opcode = fetch();
    const Instruction& in = instructionTable()[opcode];
    switch (in.kind) {
    case AccessKind::Read: executeRead(in); break;
    case AccessKind::Write: executeWrite(in); break;
    case AccessKind::ReadModifyWrite: executeModify(in); break;
    case AccessKind::Implied: executeImplied(in); break;
    case AccessKind::Branch: executeBranch(in); break;
    case AccessKind::Control: (this->*in.control)(); break;
    case AccessKind::Unsupported:
        pc = opcodePc;
        throw UnsupportedOpcodeError(opcode, opcodePc);
    }
    ++instructions_;
    nmiRequested_ = pollPrevious_.nmi;
    irqRequested_ = !nmiRequested_ && pollPrevious_.irq;
    return static_cast<unsigned>(cycles_ - start);
}

// Effective-address calculation with the dummy reads performed by the NMOS
// 6502. Indexed modes first read from the un-carried address; reads only
// spend the extra cycle when a page boundary is crossed, writes and RMW
// instructions always do.
std::uint16_t CPU6502::resolveAddress(AddressingMode mode, bool alwaysFix) {
    switch (mode) {
    case M::Immediate: return pc++;
    case M::ZeroPage: return fetch();
    case M::ZeroPageX:
    case M::ZeroPageY: {
        const std::uint8_t base = fetch();
        read(base);
        return static_cast<std::uint8_t>(base + (mode == M::ZeroPageX ? x : y));
    }
    case M::Absolute: {
        const std::uint8_t lo = fetch();
        const std::uint8_t hi = fetch();
        return static_cast<std::uint16_t>(lo | (hi << 8));
    }
    case M::AbsoluteX:
    case M::AbsoluteY: {
        const std::uint8_t lo = fetch();
        const std::uint8_t hi = fetch();
        const unsigned sum = lo + (mode == M::AbsoluteX ? x : y);
        const auto partial = static_cast<std::uint16_t>((hi << 8) | (sum & 0xFFu));
        if (sum > 0xFFu || alwaysFix) read(partial);
        return static_cast<std::uint16_t>(partial + (sum > 0xFFu ? 0x100u : 0u));
    }
    case M::IndirectX: {
        std::uint8_t pointer = fetch();
        read(pointer);
        pointer = static_cast<std::uint8_t>(pointer + x);
        const std::uint8_t lo = read(pointer);
        const std::uint8_t hi = read(static_cast<std::uint8_t>(pointer + 1));
        return static_cast<std::uint16_t>(lo | (hi << 8));
    }
    case M::IndirectY: {
        const std::uint8_t pointer = fetch();
        const std::uint8_t lo = read(pointer);
        const std::uint8_t hi = read(static_cast<std::uint8_t>(pointer + 1));
        const unsigned sum = lo + y;
        const auto partial = static_cast<std::uint16_t>((hi << 8) | (sum & 0xFFu));
        if (sum > 0xFFu || alwaysFix) read(partial);
        return static_cast<std::uint16_t>(partial + (sum > 0xFFu ? 0x100u : 0u));
    }
    default:
        return pc;
    }
}

void CPU6502::executeRead(const Instruction& in) {
    const std::uint16_t address = resolveAddress(in.mode, false);
    (this->*in.read)(read(address));
}

void CPU6502::executeWrite(const Instruction& in) {
    std::uint16_t address = resolveAddress(in.mode, true);
    std::uint8_t value = (this->*in.write)();
    if (in.write == &CPU6502::opAHX || in.write == &CPU6502::opTAS ||
        in.write == &CPU6502::opSHX || in.write == &CPU6502::opSHY) {
        // Unstable indexed high-byte stores: mask with unindexed address
        // high byte + 1. On page crossing, the data value can also replace
        // the effective address high byte on common NMOS 6502 revisions.
        const std::uint8_t index = in.mode == M::AbsoluteX ? x : y;
        const auto base = static_cast<std::uint16_t>(address - index);
        value &= static_cast<std::uint8_t>((base >> 8) + 1);
        if ((base & 0xFF00u) != (address & 0xFF00u))
            address = static_cast<std::uint16_t>((value << 8) | (address & 0x00FFu));
    }
    write(address, value);
}

void CPU6502::executeModify(const Instruction& in) {
    if (in.mode == M::Accumulator) {
        read(pc);
        a = (this->*in.modify)(a);
        return;
    }
    const std::uint16_t address = resolveAddress(in.mode, true);
    const std::uint8_t value = read(address);
    write(address, value); // NMOS RMW writes the unmodified value back first.
    write(address, (this->*in.modify)(value));
}

void CPU6502::executeImplied(const Instruction& in) {
    read(pc);
    (this->*in.implied)();
}

void CPU6502::executeBranch(const Instruction& in) {
    const auto offset = static_cast<std::int8_t>(fetch());
    // Interrupts are polled before the operand fetch; a taken branch that
    // stays on the same page does not poll again, so it keeps that sample.
    const InterruptPoll beforeOperand = pollPrevious_;
    const bool taken = ((p & in.branchFlag) != 0) == in.branchIfSet;
    if (!taken) return;
    read(pc);
    const auto target = static_cast<std::uint16_t>(pc + offset);
    if ((target & 0xFF00u) != (pc & 0xFF00u)) {
        read(static_cast<std::uint16_t>((pc & 0xFF00u) | (target & 0x00FFu)));
    } else {
        pollPrevious_ = beforeOperand;
    }
    pc = target;
}

// ---- ALU -------------------------------------------------------------------
void CPU6502::addWithCarry(std::uint8_t v) {
    // Binary only: the 2A03 has the decimal-mode circuitry disconnected.
    const unsigned sum = a + v + (p & Flag::C);
    const bool overflow = (~(a ^ v) & (a ^ sum) & 0x80u) != 0;
    p = static_cast<std::uint8_t>((p & ~(Flag::C | Flag::V)) | (sum > 0xFFu ? Flag::C : 0) |
                                  (overflow ? Flag::V : 0));
    a = static_cast<std::uint8_t>(sum);
    setZN(a);
}
void CPU6502::compare(std::uint8_t reg, std::uint8_t v) {
    p = static_cast<std::uint8_t>((p & ~Flag::C) | (reg >= v ? Flag::C : 0));
    setZN(static_cast<std::uint8_t>(reg - v));
}
void CPU6502::opLDA(std::uint8_t v) { a = v; setZN(a); }
void CPU6502::opLDX(std::uint8_t v) { x = v; setZN(x); }
void CPU6502::opLDY(std::uint8_t v) { y = v; setZN(y); }
void CPU6502::opADC(std::uint8_t v) { addWithCarry(v); }
void CPU6502::opSBC(std::uint8_t v) { addWithCarry(static_cast<std::uint8_t>(~v)); }
void CPU6502::opAND(std::uint8_t v) { a &= v; setZN(a); }
void CPU6502::opORA(std::uint8_t v) { a |= v; setZN(a); }
void CPU6502::opEOR(std::uint8_t v) { a ^= v; setZN(a); }
void CPU6502::opCMP(std::uint8_t v) { compare(a, v); }
void CPU6502::opCPX(std::uint8_t v) { compare(x, v); }
void CPU6502::opCPY(std::uint8_t v) { compare(y, v); }
void CPU6502::opBIT(std::uint8_t v) {
    p = static_cast<std::uint8_t>((p & ~(Flag::N | Flag::V | Flag::Z)) | (v & (Flag::N | Flag::V)) |
                                  ((a & v) == 0 ? Flag::Z : 0));
}
std::uint8_t CPU6502::opSTA() { return a; }
std::uint8_t CPU6502::opSTX() { return x; }
std::uint8_t CPU6502::opSTY() { return y; }
std::uint8_t CPU6502::opASL(std::uint8_t v) {
    p = static_cast<std::uint8_t>((p & ~Flag::C) | (v >> 7));
    v = static_cast<std::uint8_t>(v << 1); setZN(v); return v;
}
std::uint8_t CPU6502::opLSR(std::uint8_t v) {
    p = static_cast<std::uint8_t>((p & ~Flag::C) | (v & 1u));
    v = static_cast<std::uint8_t>(v >> 1); setZN(v); return v;
}
std::uint8_t CPU6502::opROL(std::uint8_t v) {
    const std::uint8_t carry = p & Flag::C;
    p = static_cast<std::uint8_t>((p & ~Flag::C) | (v >> 7));
    v = static_cast<std::uint8_t>((v << 1) | carry); setZN(v); return v;
}
std::uint8_t CPU6502::opROR(std::uint8_t v) {
    const std::uint8_t carry = p & Flag::C;
    p = static_cast<std::uint8_t>((p & ~Flag::C) | (v & 1u));
    v = static_cast<std::uint8_t>((v >> 1) | (carry << 7)); setZN(v); return v;
}
std::uint8_t CPU6502::opINC(std::uint8_t v) { ++v; setZN(v); return v; }
std::uint8_t CPU6502::opDEC(std::uint8_t v) { --v; setZN(v); return v; }
void CPU6502::opCLC() { p &= ~Flag::C; }
void CPU6502::opSEC() { p |= Flag::C; }
void CPU6502::opCLI() { p &= ~Flag::I; }
void CPU6502::opSEI() { p |= Flag::I; }
void CPU6502::opCLD() { p &= ~Flag::D; }
void CPU6502::opSED() { p |= Flag::D; }
void CPU6502::opCLV() { p &= ~Flag::V; }
void CPU6502::opTAX() { x = a; setZN(x); }
void CPU6502::opTAY() { y = a; setZN(y); }
void CPU6502::opTXA() { a = x; setZN(a); }
void CPU6502::opTYA() { a = y; setZN(a); }
void CPU6502::opTSX() { x = s; setZN(x); }
void CPU6502::opTXS() { s = x; }
void CPU6502::opINX() { ++x; setZN(x); }
void CPU6502::opINY() { ++y; setZN(y); }
void CPU6502::opDEX() { --x; setZN(x); }
void CPU6502::opDEY() { --y; setZN(y); }
void CPU6502::opNOP() {}
void CPU6502::opKIL() { read(pc); halted_ = true; }
void CPU6502::opNOPRead(std::uint8_t) {}
// The constant $EE is a common empirical approximation, not a universal
// electrical property of 2A03 silicon. Keep it visible and testable.
void CPU6502::opXAA(std::uint8_t v) {
    a = static_cast<std::uint8_t>((a | 0xEEu) & x & v);
    setZN(a);
}
void CPU6502::opLXA(std::uint8_t v) {
    a = x = static_cast<std::uint8_t>((a | 0xEEu) & v);
    setZN(a);
}
void CPU6502::opLAX(std::uint8_t v) { a = x = v; setZN(v); }
void CPU6502::opLAS(std::uint8_t v) { s = a = x = static_cast<std::uint8_t>(v & s); setZN(a); }
std::uint8_t CPU6502::opSAX() { return static_cast<std::uint8_t>(a & x); }
std::uint8_t CPU6502::opAHX() { return static_cast<std::uint8_t>(a & x); }
std::uint8_t CPU6502::opTAS() { s = static_cast<std::uint8_t>(a & x); return s; }
std::uint8_t CPU6502::opSHX() { return x; }
std::uint8_t CPU6502::opSHY() { return y; }
void CPU6502::opANC(std::uint8_t v) {
    opAND(v);
    p = static_cast<std::uint8_t>((p & ~Flag::C) | ((a & 0x80u) ? Flag::C : 0));
}
void CPU6502::opALR(std::uint8_t v) {
    opAND(v);
    a = opLSR(a);
}
void CPU6502::opARR(std::uint8_t v) {
    const std::uint8_t masked = static_cast<std::uint8_t>(a & v);
    const std::uint8_t carryIn = (p & Flag::C) ? 0x80u : 0u;
    a = static_cast<std::uint8_t>((masked >> 1) | carryIn);
    setZN(a);
    p = static_cast<std::uint8_t>((p & ~(Flag::C | Flag::V)) |
        ((a & 0x40u) ? Flag::C : 0) |
        (((a ^ (a << 1)) & 0x40u) ? Flag::V : 0));
}
void CPU6502::opAXS(std::uint8_t v) {
    const std::uint8_t both = static_cast<std::uint8_t>(a & x);
    p = static_cast<std::uint8_t>((p & ~Flag::C) | (both >= v ? Flag::C : 0));
    x = static_cast<std::uint8_t>(both - v);
    setZN(x);
}
std::uint8_t CPU6502::opSLO(std::uint8_t v) { v = opASL(v); opORA(v); return v; }
std::uint8_t CPU6502::opRLA(std::uint8_t v) { v = opROL(v); opAND(v); return v; }
std::uint8_t CPU6502::opSRE(std::uint8_t v) { v = opLSR(v); opEOR(v); return v; }
std::uint8_t CPU6502::opRRA(std::uint8_t v) { v = opROR(v); opADC(v); return v; }
std::uint8_t CPU6502::opDCP(std::uint8_t v) { v = opDEC(v); opCMP(v); return v; }
std::uint8_t CPU6502::opISC(std::uint8_t v) { v = opINC(v); opSBC(v); return v; }

// ---- Control flow ------------------------------------------------------------
void CPU6502::opJMPAbs() {
    const std::uint8_t lo = fetch();
    const std::uint8_t hi = read(pc);
    pc = static_cast<std::uint16_t>(lo | (hi << 8));
}
void CPU6502::opJMPInd() {
    const std::uint8_t plo = fetch();
    const std::uint8_t phi = fetch();
    const auto pointer = static_cast<std::uint16_t>(plo | (phi << 8));
    const std::uint8_t lo = read(pointer);
    // NMOS bug: the pointer's high byte is fetched without carrying into the
    // page, so JMP ($xxFF) reads its high byte from $xx00.
    const std::uint8_t hi = read(static_cast<std::uint16_t>((pointer & 0xFF00u) | ((pointer + 1u) & 0x00FFu)));
    pc = static_cast<std::uint16_t>(lo | (hi << 8));
}
void CPU6502::opJSR() {
    const std::uint8_t lo = fetch();
    read(static_cast<std::uint16_t>(0x0100u | s)); // Internal stack pointer cycle.
    push(static_cast<std::uint8_t>(pc >> 8));      // PC points at the high operand byte.
    push(static_cast<std::uint8_t>(pc));
    const std::uint8_t hi = read(pc);
    pc = static_cast<std::uint16_t>(lo | (hi << 8));
}
void CPU6502::opRTS() {
    read(pc);
    read(static_cast<std::uint16_t>(0x0100u | s));
    const std::uint8_t lo = pull();
    const std::uint8_t hi = pull();
    pc = static_cast<std::uint16_t>(lo | (hi << 8));
    read(pc);
    ++pc;
}
void CPU6502::opRTI() {
    read(pc);
    read(static_cast<std::uint16_t>(0x0100u | s));
    p = static_cast<std::uint8_t>((pull() & ~Flag::B) | Flag::U);
    const std::uint8_t lo = pull();
    const std::uint8_t hi = pull();
    pc = static_cast<std::uint16_t>(lo | (hi << 8));
}
void CPU6502::opBRK() {
    fetch(); // Padding byte: BRK returns to PC+2.
    push(static_cast<std::uint8_t>(pc >> 8));
    push(static_cast<std::uint8_t>(pc));
    const bool hijacked = bus_.nmiPending();
    push(static_cast<std::uint8_t>(p | Flag::B | Flag::U));
    p |= Flag::I;
    const std::uint16_t vector = hijacked ? 0xFFFA : 0xFFFE;
    if (hijacked) bus_.acknowledgeNmi();
    const std::uint8_t lo = read(vector);
    const std::uint8_t hi = read(static_cast<std::uint16_t>(vector + 1));
    pc = static_cast<std::uint16_t>(lo | (hi << 8));
    pollNow_ = pollPrevious_ = InterruptPoll{};
}
void CPU6502::opPHA() { read(pc); push(a); }
void CPU6502::opPHP() { read(pc); push(static_cast<std::uint8_t>(p | Flag::B | Flag::U)); }
void CPU6502::opPLA() {
    read(pc);
    read(static_cast<std::uint16_t>(0x0100u | s));
    a = pull();
    setZN(a);
}
void CPU6502::opPLP() {
    read(pc);
    read(static_cast<std::uint16_t>(0x0100u | s));
    const std::uint8_t value = pull();
    // The I flag change is visible to interrupt polling only after this
    // instruction, which is why the poll sample is taken before updating P.
    p = static_cast<std::uint8_t>((value & ~Flag::B) | Flag::U);
}

// ---- Debugging ---------------------------------------------------------------
std::string CPU6502::disassemble(std::uint16_t address, unsigned* length) const {
    const std::uint8_t opcode = bus_.peek(address);
    const Instruction& in = instructionTable()[opcode];
    const unsigned len = in.kind == AccessKind::Unsupported ? 1 : instructionLength(in.mode);
    if (length) *length = len;
    const std::uint8_t b1 = bus_.peek(static_cast<std::uint16_t>(address + 1));
    const std::uint8_t b2 = bus_.peek(static_cast<std::uint16_t>(address + 2));
    const unsigned word = b1 | (b2 << 8);
    char operand[32] = "";
    switch (in.mode) {
    case M::Implied: break;
    case M::Accumulator: std::snprintf(operand, sizeof operand, "A"); break;
    case M::Immediate: std::snprintf(operand, sizeof operand, "#$%02X", b1); break;
    case M::ZeroPage: std::snprintf(operand, sizeof operand, "$%02X", b1); break;
    case M::ZeroPageX: std::snprintf(operand, sizeof operand, "$%02X,X", b1); break;
    case M::ZeroPageY: std::snprintf(operand, sizeof operand, "$%02X,Y", b1); break;
    case M::Absolute: std::snprintf(operand, sizeof operand, "$%04X", word); break;
    case M::AbsoluteX: std::snprintf(operand, sizeof operand, "$%04X,X", word); break;
    case M::AbsoluteY: std::snprintf(operand, sizeof operand, "$%04X,Y", word); break;
    case M::IndirectX: std::snprintf(operand, sizeof operand, "($%02X,X)", b1); break;
    case M::IndirectY: std::snprintf(operand, sizeof operand, "($%02X),Y", b1); break;
    case M::Indirect: std::snprintf(operand, sizeof operand, "($%04X)", word); break;
    case M::Relative:
        std::snprintf(operand, sizeof operand, "$%04X",
                      static_cast<unsigned>(static_cast<std::uint16_t>(address + 2 + static_cast<std::int8_t>(b1))));
        break;
    }
    std::string text = in.kind == AccessKind::Unsupported ? std::string("???") : std::string(in.mnemonic);
    if (operand[0]) { text += ' '; text += operand; }
    return text;
}

std::string CPU6502::traceLine() const {
    unsigned length = 1;
    const std::string text = disassemble(pc, &length);
    char bytes[16] = "";
    int used = 0;
    for (unsigned i = 0; i < 3; ++i) {
        if (i < length)
            used += std::snprintf(bytes + used, sizeof bytes - used, "%02X ",
                                  bus_.peek(static_cast<std::uint16_t>(pc + i)));
        else
            used += std::snprintf(bytes + used, sizeof bytes - used, "   ");
    }
    char line[160];
    std::snprintf(line, sizeof line, "%04X  %s %-31s A:%02X X:%02X Y:%02X P:%02X SP:%02X CYC:%llu",
                  pc, bytes, text.c_str(), a, x, y, p, s, static_cast<unsigned long long>(cycles_));
    return line;
}

} // namespace frantastic::nes
