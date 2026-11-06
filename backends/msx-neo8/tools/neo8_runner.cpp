// Execute a linked ROM in a deliberately bounded MSX profile, not a full MSX.
// CPU execution is supplied by the unmodified, pinned floooh/chips core.
#define CHIPS_IMPL
#include "../vendor/chips/z80.h"
#include <algorithm>
#include <array>
#include <fstream>
#include <iostream>
#include <iterator>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
unsigned number(const char* arg) {
    std::string s(arg);
    std::size_t end = 0;
    auto n = std::stoul(s, &end, 0);
    if (s.empty() || s[0] == '-' || end != s.size() || n > 0xffffffffUL)
        throw std::runtime_error("invalid unsigned number: " + s);
    return static_cast<unsigned>(n);
}

struct Machine {
    std::vector<uint8_t> rom;
    std::vector<uint8_t> mapperRam;
    unsigned ramSegment=0, mapperSwitches=0;
    bool mapperPage2=false;
    std::array<uint8_t, 16384> ram;
    std::array<uint8_t, 16384> bios{};
    std::array<uint16_t, 6> banks{};
    std::set<unsigned> fetchedSegments;
    std::set<unsigned> selectedB;
    std::vector<unsigned> output;
    z80_t cpu{};
    uint64_t pins = 0, ticks = 0;
    unsigned floor = 0xef80, himem = 0xf380, minSp = 0xffff;
    unsigned irqPeriod = 0, interrupts = 0, switches = 0, slotCalls = 0;
    unsigned shadow = 0, fault = 0;
    bool expanded = false, page2 = false, irqPending = false;

    void stub(unsigned address, std::initializer_list<uint8_t> bytes) {
        std::copy(bytes.begin(), bytes.end(), bios.begin() + address);
    }
    void start() {
        if (rom.size() < 32768 || rom.size() > 16384ULL * 8192 || rom.size() % 8192 || rom[0] != 'A' || rom[1] != 'B' ||
            std::string(rom.begin() + 16, rom.begin() + 24) != "ROM_NEO8")
            throw std::runtime_error("expected a NEO-8 ROM with AB header");
        ram.fill(0xa5); // Detect initialization that accidentally relies on zero RAM.
        ram[0x3c4a] = static_cast<uint8_t>(himem);
        ram[0x3c4b] = static_cast<uint8_t>(himem >> 8);
        for (unsigned i = 0; i < 4; ++i) {
            ram[0x3cc1 + i] = (expanded && i == 1) ? 0x80 : 0;
            ram[0x3cc5 + i] = (expanded && i == 1) ? 0x08 : 0;
        }
        bios.fill(0xff);
        // Real Z80 instructions implement the three supported BIOS contracts.
        // Ports F0/F1 are private test devices, not MSX hardware interfaces.
        stub(0x0138, {0xdb, 0xa8, 0xc9});                 // RSLREG: IN A,(A8); RET
        stub(0x0024, {0xf3, 0xd3, 0xf0, 0xc9});           // ENASLT: DI; OUT(F0),A; RET
        stub(0x00a2, {0xd3, 0xf1,                         // CHPUT: output A; clobber registers
                      0x01, 0x11, 0x11, 0x11, 0x22, 0x22, 0x21, 0x33, 0x33,
                      0xdd, 0x21, 0x44, 0x44, 0xfd, 0x21, 0x55, 0x55, 0xc9});
        // Synthetic IM1 handler: save main/alternate registers, then restore.
        // This models a declared interrupt workload, not the real BIOS ISR.
        stub(0x0038, {0xf5, 0xc5, 0xd5, 0xe5, 0xdd, 0xe5, 0xfd, 0xe5,
                      0x08, 0xd9, 0xf5, 0xc5, 0xd5, 0xe5,
                      0xe1, 0xd1, 0xc1, 0xf1, 0xd9, 0x08,
                      0xfd, 0xe1, 0xdd, 0xe1, 0xe1, 0xd1, 0xc1, 0xf1, 0xfb, 0xed, 0x4d});
        z80_init(&cpu);
        cpu.sp = static_cast<uint16_t>(himem);
        cpu.im = 1;
        pins = z80_prefetch(&cpu, static_cast<uint16_t>(rom[2] | (rom[3] << 8)));
    }
    bool cartridge(unsigned address) const {
        return address >= 0x4000 && address < (page2 ? 0xc000 : 0x8000);
    }
    unsigned word(unsigned address) const {
        return ram.at(address - 0xc000) | (ram.at(address + 1 - 0xc000) << 8);
    }
    uint8_t read(unsigned address, bool fetch) {
        if (address >= 0xc000) return ram[address - 0xc000];
        if(mapperPage2 && address>=0x8000) return mapperRam.at(ramSegment*16384+address-0x8000);
        if (address < 0x4000) {
            if (fetch && bios[address] == 0xff)
                throw std::runtime_error("unsupported BIOS entry or execution in absent memory at " + std::to_string(address));
            return bios[address];
        }
        if (!cartridge(address)) throw std::runtime_error("read from page 2 before cartridge slot selection");
        unsigned segment = banks[address >> 13];
        std::size_t offset = std::size_t(segment) * 8192 + (address & 8191);
        if (offset >= rom.size()) throw std::runtime_error("read from a segment absent from this ROM");
        if (fetch) fetchedSegments.insert(segment);
        return rom[offset];
    }
    void write(unsigned address, uint8_t value) {
        if (address >= 0xc000) { ram[address - 0xc000] = value; return; }
        if(mapperPage2 && address>=0x8000) {mapperRam.at(ramSegment*16384+address-0x8000)=value;return;}
        if (!cartridge(address)) throw std::runtime_error("write outside physical RAM or selected cartridge");
        unsigned reg = (address >> 11) & 7;
        if (reg < 2) return;
        auto& bank = banks[reg - 2];
        if (address & 1) {
            if (value & 0xc0) throw std::runtime_error("write to reserved NEO segment bits");
            bank = static_cast<uint16_t>((bank & 255) | (value << 8));
            if (reg == 5) selectedB.insert(bank);
            ++switches;
        } else bank = static_cast<uint16_t>((bank & 0x3f00) | value);
    }
    void tick() {
        ++ticks;
        if (irqPeriod && ticks % irqPeriod == 0) irqPending = true;
        if (irqPending) pins |= Z80_INT; else pins &= ~Z80_INT;
        pins = z80_tick(&cpu, pins);
        if (z80_opdone(&cpu)) {
            minSp = std::min(minSp, unsigned(cpu.sp));
            if (page2 && cpu.sp < floor)
                throw std::runtime_error("observed stack exceeded its reserved budget");
        }
        if (pins & Z80_MREQ) {
            unsigned addr = Z80_GET_ADDR(pins);
            if (pins & Z80_RD) {
                uint8_t data = read(addr, (pins & Z80_M1) != 0);
                Z80_SET_DATA(pins, data);
            } else if (pins & Z80_WR) write(addr, Z80_GET_DATA(pins));
        } else if (pins & Z80_IORQ) {
            if (pins & Z80_M1) {
                if(mapperPage2) throw std::runtime_error("interrupt observed a borrowed mapper window");
                ++interrupts;
                irqPending = false;
                pins &= ~Z80_INT;
                Z80_SET_DATA(pins, 0xff);
                if (page2 && shadow && word(shadow) != banks[3])
                    throw std::runtime_error("interrupt observed inconsistent mapper and software shadow");
            } else {
                unsigned port = Z80_GET_ADDR(pins) & 255;
                if (pins & Z80_RD) {
                    if (port != 0xa8) throw std::runtime_error("unsupported IO read");
                    Z80_SET_DATA(pins, 0xc4); // BIOS=0, cartridge=1, page2 initially empty, RAM=3
                } else if (pins & Z80_WR) {
                    if (port == 0xf0) {
                        auto slot=Z80_GET_DATA(pins);
                        if (cpu.h != 0x80 || (slot != (expanded ? 0x89 : 1) && (slot!=3 || mapperRam.empty())))
                            throw std::runtime_error("ENASLT received incorrect page or cartridge slot");
                        mapperPage2=slot==3;page2 = !mapperPage2;
                        ++slotCalls;
                    } else if (port == 0xf1) output.push_back(Z80_GET_DATA(pins));
                    else if(port==0xfe && !mapperRam.empty()) {
                        ramSegment=Z80_GET_DATA(pins);++mapperSwitches;
                        if(ramSegment>=mapperRam.size()/16384) throw std::runtime_error("selecting unowned RAM mapper segment");
                    }
                    else throw std::runtime_error("unsupported IO write");
                }
            }
        }
    }
    void report() const {
        std::cout << "{\"model\":\"neo8-bios-stubs-1\",\"stop\":\"first-halt\",\"ticks\":" << ticks
                  << ",\"pc\":" << cpu.pc << ",\"sp\":" << cpu.sp << ",\"minimum_sp\":" << minSp
                  << ",\"observed_stack_bytes\":" << (minSp < 0xf380 ? 0xf380 - minSp : 0)
                  << ",\"interrupts_enabled\":" << (cpu.iff1 ? "true" : "false")
                  << ",\"interrupts_accepted\":" << interrupts << ",\"mapper_word_writes\":" << switches
                  << ",\"page2_slot_calls\":" << slotCalls << ",\"bank_b\":" << banks[3]
                  << ",\"ram_mapper_switches\":" << mapperSwitches << ",\"mapper_page2\":" << (mapperPage2 ? "true" : "false")
                  << ",\"fault\":" << (fault && page2 ? unsigned(ram.at(fault - 0xc000)) : 0)
                  << ",\"fetched_segments\":[";
        bool first = true;
        for (auto s : fetchedSegments) { if (!first) std::cout << ','; first = false; std::cout << s; }
        std::cout << "],\"bank_b_segments_selected\":[";
        first = true;
        for (auto s : selectedB) { if (!first) std::cout << ','; first = false; std::cout << s; }
        std::cout << "],\"output_bytes\":[";
        first = true;
        for (auto c : output) { if (!first) std::cout << ','; first = false; std::cout << c; }
        std::cout << "]}\n";
    }
};
}

int main(int argc, char** argv) {
    try {
        Machine m;
        unsigned limit = 10000000;
        std::string romPath, dumpPath;
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--expanded-slot") { m.expanded = true; continue; }
            if (++i >= argc) throw std::runtime_error("missing value for " + arg);
            if (arg == "--rom") romPath = argv[i];
            else if (arg == "--dump-ram") dumpPath = argv[i];
            else if (arg == "--max-ticks") limit = number(argv[i]);
            else if (arg == "--stack-floor") m.floor = number(argv[i]);
            else if (arg == "--himem") m.himem = number(argv[i]);
            else if (arg == "--irq-period") m.irqPeriod = number(argv[i]);
            else if (arg == "--shadow") m.shadow = number(argv[i]);
            else if (arg == "--fault") m.fault = number(argv[i]);
            else if (arg == "--ram-segments") {
                auto count=number(argv[i]);if(count>256) throw std::runtime_error("too many RAM segments");
                m.mapperRam.resize(count*16384,0xa5);
            }
            else throw std::runtime_error("unknown argument: " + arg);
        }
        if (m.floor < 0xc000 || m.floor >= 0xf380 || m.himem < 0xc000 || m.himem > 0xffff ||
            (m.shadow && (m.shadow < 0xc000 || m.shadow >= m.floor - 1)) ||
            (m.fault && (m.fault < 0xc000 || m.fault >= m.floor)) || !limit)
            throw std::runtime_error("invalid machine profile limits or runtime symbol address");
        std::ifstream file(romPath, std::ios::binary);
        if (!file) throw std::runtime_error("cannot open ROM: " + romPath);
        m.rom.assign(std::istreambuf_iterator<char>(file), {});
        m.start();
        while (!(m.pins & Z80_HALT) && m.ticks < limit) m.tick();
        if (!(m.pins & Z80_HALT)) throw std::runtime_error("execution exhausted its tick budget");
        if (!dumpPath.empty()) {
            std::ofstream dump(dumpPath, std::ios::binary);
            dump.write(reinterpret_cast<const char*>(m.ram.data()), m.ram.size());
            if (!dump) throw std::runtime_error("cannot write RAM snapshot");
        }
        m.report();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "NEO-8 execution failed: " << e.what() << '\n';
        return 1;
    }
}
