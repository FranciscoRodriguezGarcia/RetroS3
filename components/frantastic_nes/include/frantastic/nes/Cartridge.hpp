#pragma once
#include "frantastic/nes/Mapper.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace frantastic::nes {

class RomFormatError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

enum class RomFormat { INes, Nes20 };

// Parsed iNES / NES 2.0 image. Only header facts and raw ROM data live here;
// the board behaviour belongs to a Mapper implementation.
struct RomImage {
    RomFormat format = RomFormat::INes;
    unsigned mapper = 0;
    unsigned submapper = 0;
    Mirroring mirroring = Mirroring::Horizontal;
    bool battery = false;
    bool trainer = false;
    std::size_t prgRamSize = 0;   // Volatile + battery-backed PRG RAM in bytes.
    std::size_t chrRamSize = 0;   // Non-zero when the board uses CHR RAM.
    std::vector<std::uint8_t> trainerData;
    std::vector<std::uint8_t> prg;
    std::vector<std::uint8_t> chr;  // Empty when the cartridge uses CHR RAM.

    static RomImage parse(const std::vector<std::uint8_t>& file);
};

const char* mapperName(unsigned mapper);

// Owns the ROM image and the mapper board created for it.
class Cartridge {
public:
    explicit Cartridge(RomImage image);
    static std::unique_ptr<Cartridge> fromFile(const std::string& path);
    static std::unique_ptr<Cartridge> fromBytes(const std::vector<std::uint8_t>& bytes);

    const RomImage& image() const { return image_; }
    Mapper& mapper() { return *mapper_; }
    const Mapper& mapper() const { return *mapper_; }
    void reset() { mapper_->reset(); }
    std::string describe() const;

private:
    RomImage image_;
    std::unique_ptr<Mapper> mapper_;
};

// Mapper factory: throws UnsupportedMapperError with a descriptive message.
std::unique_ptr<Mapper> createMapper(const RomImage& image);

} // namespace frantastic::nes
