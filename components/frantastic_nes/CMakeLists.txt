idf_component_register(
    SRCS
        "src/nes/APU.cpp"
        "src/nes/Bus.cpp"
        "src/nes/CPU6502.cpp"
        "src/nes/Cartridge.cpp"
        "src/nes/Controller.cpp"
        "src/nes/Mapper.cpp"
        "src/nes/Mapper000.cpp"
        "src/nes/Mapper001.cpp"
        "src/nes/Mapper004.cpp"
        "src/nes/Mapper005.cpp"
        "src/nes/Mapper019.cpp"
        "src/nes/Mapper069.cpp"
        "src/nes/Mapper091.cpp"
        "src/nes/Mapper206.cpp"
        "src/nes/Mapper210.cpp"
        "src/nes/MapperBandai.cpp"
        "src/nes/MapperDiscrete.cpp"
        "src/nes/MapperJalecoTaito.cpp"
        "src/nes/MapperMmcLatch.cpp"
        "src/nes/MapperVrc24.cpp"
        "src/nes/MapperVrc6.cpp"
        "src/nes/NES.cpp"
        "src/nes/PPU.cpp"
    INCLUDE_DIRS "include"
)

# Frantastic NES uses C++ exceptions for ROM validation and mapper errors.
target_compile_options(${COMPONENT_LIB} PRIVATE -fexceptions)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
