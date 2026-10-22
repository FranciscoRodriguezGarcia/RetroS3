#include "frantastic/nes/Cartridge.hpp"
#include "frantastic/nes/Mapper000.hpp"
#include "frantastic/nes/Mapper001.hpp"
#include "frantastic/nes/Mapper004.hpp"
#include "frantastic/nes/Mapper005.hpp"
#include "frantastic/nes/MapperDiscrete.hpp"
#include "frantastic/nes/MapperMmcLatch.hpp"
#include "frantastic/nes/Mapper069.hpp"
#include "frantastic/nes/MapperVrc6.hpp"
#include "frantastic/nes/MapperVrc24.hpp"
#include "frantastic/nes/Mapper019.hpp"
#include "frantastic/nes/Mapper091.hpp"
#include "frantastic/nes/Mapper210.hpp"
#include "frantastic/nes/MapperJalecoTaito.hpp"
#include "frantastic/nes/Mapper206.hpp"
#include "frantastic/nes/MapperBandai.hpp"
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>

namespace frantastic::nes {

namespace {
std::size_t nes20RomSize(unsigned lsb, unsigned msbNibble, std::size_t unit) {
    if (msbNibble == 0x0Fu) {
        // Exponent-multiplier notation: 2^E * (MM*2+1).
        const unsigned exponent = lsb >> 2;
        const unsigned multiplier = (lsb & 3u) * 2u + 1u;
        if (exponent > 30) throw RomFormatError("NES: NES 2.0 ROM size exponent too large");
        return (std::size_t{1} << exponent) * multiplier;
    }
    return ((static_cast<std::size_t>(msbNibble) << 8) | lsb) * unit;
}
std::size_t nes20RamSize(unsigned shift) { return shift ? (std::size_t{64} << shift) : 0; }
} // namespace

const char* mapperName(unsigned mapper) {
    switch (mapper) {
    case 0: return "NROM";
    case 1: return "MMC1";
    case 2: return "UxROM";
    case 3: return "CNROM";
    case 4: return "MMC3";
    case 5: return "MMC5";
    case 7: return "AxROM";
    case 9: return "MMC2";
    case 10: return "MMC4";
    case 16: return "Bandai FCG/LZ93D50";
    case 19: return "Namco 163";
    case 18: return "Jaleco SS88006";
    case 33: return "Taito TC0190";
    case 153: return "Bandai LZ93D50 WRAM";
    case 159: return "Bandai LZ93D50 X24C01";
    case 21: case 22: case 23: case 25: return "Konami VRC2/VRC4";
    case 24: return "Konami VRC6a";
    case 26: return "Konami VRC6b";
    case 34: return "BNROM/NINA-001";
    case 66: return "GNROM";
    case 69: return "Sunsoft FME-7/5B";
    case 91: return "JY Company ASIC";
    case 210: return "Namco 175 / 340";
    case 206: return "Namco 108 / Tengen MIMIC-1";
    default: return "unknown";
    }
}

RomImage RomImage::parse(const std::vector<std::uint8_t>& file) {
    if (file.size() < 16 || file[0] != 'N' || file[1] != 'E' || file[2] != 'S' || file[3] != 0x1A)
        throw RomFormatError("NES: missing iNES signature (expected 'NES' 0x1A)");
    const std::uint8_t f6 = file[6], f7 = file[7];
    RomImage image;
    image.format = ((f7 & 0x0Cu) == 0x08u) ? RomFormat::Nes20 : RomFormat::INes;
    image.trainer = (f6 & 0x04u) != 0;
    image.battery = (f6 & 0x02u) != 0;
    image.mirroring = (f6 & 0x08u) ? Mirroring::FourScreen : ((f6 & 0x01u) ? Mirroring::Vertical : Mirroring::Horizontal);

    std::size_t prgSize, chrSize;
    if (image.format == RomFormat::Nes20) {
        image.mapper = (f6 >> 4) | (f7 & 0xF0u) | ((file[8] & 0x0Fu) << 8);
        image.submapper = file[8] >> 4;
        prgSize = nes20RomSize(file[4], file[9] & 0x0Fu, 0x4000u);
        chrSize = nes20RomSize(file[5], file[9] >> 4, 0x2000u);
        image.prgRamSize = nes20RamSize(file[10] & 0x0Fu) + nes20RamSize(file[10] >> 4);
        image.chrRamSize = nes20RamSize(file[11] & 0x0Fu) + nes20RamSize(file[11] >> 4);
    } else {
        // Archaic dumps often carry garbage ("DiskDude!") in bytes 7-15; in
        // that case only the low mapper nibble is trustworthy.
        bool dirty = false;
        for (int i = 12; i < 16; ++i) dirty = dirty || file[i] != 0;
        image.mapper = (f6 >> 4) | (dirty ? 0u : (f7 & 0xF0u));
        prgSize = static_cast<std::size_t>(file[4]) * 0x4000u;
        chrSize = static_cast<std::size_t>(file[5]) * 0x2000u;
        image.prgRamSize = 0x2000u;
        image.chrRamSize = chrSize ? 0 : 0x2000u;
    }
    if (prgSize == 0) throw RomFormatError("NES: header declares no PRG ROM");
    if (chrSize == 0 && image.chrRamSize == 0) image.chrRamSize = 0x2000u;

    std::size_t offset = 16;
    if (image.trainer) {
        if (file.size() < offset + 512) throw RomFormatError("NES: file truncated inside 512-byte trainer");
        image.trainerData.assign(file.begin() + 16, file.begin() + 16 + 512);
        offset += 512;
    }
    if (file.size() < offset + prgSize + chrSize) {
        std::ostringstream message;
        message << "NES: file truncated: header declares " << prgSize << " bytes PRG + " << chrSize
                << " bytes CHR but only " << (file.size() - offset) << " bytes follow the header";
        throw RomFormatError(message.str());
    }
    image.prg.assign(file.begin() + static_cast<std::ptrdiff_t>(offset), file.begin() + static_cast<std::ptrdiff_t>(offset + prgSize));
    offset += prgSize;
    image.chr.assign(file.begin() + static_cast<std::ptrdiff_t>(offset), file.begin() + static_cast<std::ptrdiff_t>(offset + chrSize));
    return image;
}

std::unique_ptr<Mapper> createMapper(const RomImage& image) {
    switch (image.mapper) {
    case 0: return std::make_unique<Mapper000>(image);
    case 1: return std::make_unique<Mapper001>(image);
    case 2: return std::make_unique<Mapper002>(image);
    case 3: return std::make_unique<Mapper003>(image);
    case 4: return std::make_unique<Mapper004>(image);
    case 5: return std::make_unique<Mapper005>(image);
    case 7: return std::make_unique<Mapper007>(image);
    case 9: case 10: return std::make_unique<MapperMmcLatch>(image);
    case 16: case 153: case 159: return std::make_unique<MapperBandai>(image);
    case 19: return std::make_unique<Mapper019>(image);
    case 18: case 33: return std::make_unique<MapperJalecoTaito>(image);
    case 21: case 22: case 23: case 25: return std::make_unique<MapperVrc24>(image);
    case 24: case 26: return std::make_unique<MapperVrc6>(image);
    case 34: return std::make_unique<Mapper034>(image);
    case 66: return std::make_unique<Mapper066>(image);
    case 69: return std::make_unique<Mapper069>(image);
    case 206: return std::make_unique<Mapper206>(image);
    case 91: return std::make_unique<Mapper091>(image);
    case 210: return std::make_unique<Mapper210>(image);
    default:
        throw UnsupportedMapperError(image.mapper, "NES: unsupported mapper " + std::to_string(image.mapper) + " (" +
                                                       mapperName(image.mapper) + "); implemented: 0 (NROM), 1 (MMC1), 2 (UxROM), 3 (CNROM), 4 (MMC3), 5 (MMC5), 7 (AxROM), 9 (MMC2), 10 (MMC4), 16 (Bandai), 19 (N163), 21/22/23/25 (VRC2/4), 34 (BNROM/NINA-001), 66 (GNROM), 69 (FME-7), 91 (JY), 153/159 (Bandai), 206 (Namco 108), 210 (Namco 175/340)");
    }
}

Cartridge::Cartridge(RomImage image) : image_(std::move(image)), mapper_(createMapper(image_)) {}

std::unique_ptr<Cartridge> Cartridge::fromBytes(const std::vector<std::uint8_t>& bytes) {
    return std::make_unique<Cartridge>(RomImage::parse(bytes));
}

std::unique_ptr<Cartridge> Cartridge::fromFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw RomFormatError("NES: cannot open ROM file: " + path);
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return fromBytes(bytes);
}

std::string Cartridge::describe() const {
    std::ostringstream out;
    out << (image_.format == RomFormat::Nes20 ? "NES 2.0" : "iNES") << ", mapper " << image_.mapper << " ("
        << mapper_->name() << "), PRG " << image_.prg.size() / 1024 << " KiB, ";
    if (image_.chr.empty()) out << "CHR RAM " << image_.chrRamSize / 1024 << " KiB";
    else out << "CHR ROM " << image_.chr.size() / 1024 << " KiB";
    out << ", " << mirroringName(image_.mirroring) << " mirroring";
    if (image_.battery) out << ", battery";
    if (image_.trainer) out << ", trainer";
    return out.str();
}

} // namespace frantastic::nes
