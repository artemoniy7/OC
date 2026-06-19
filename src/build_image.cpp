#include <algorithm>
#include <array>
#include <cstdint>
#include <cstddef>
#include <initializer_list>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {
constexpr std::size_t kBootSectorSize = 512;
constexpr std::size_t kBootSignatureOffset = 510;

void append(std::vector<std::uint8_t>& out, std::initializer_list<std::uint8_t> bytes) {
    out.insert(out.end(), bytes.begin(), bytes.end());
}

void append_byte(std::vector<std::uint8_t>& out, std::uint8_t byte) {
    out.push_back(byte);
}

void append_string(std::vector<std::uint8_t>& out, std::string_view text) {
    out.insert(out.end(), text.begin(), text.end());
}

std::array<std::uint8_t, kBootSectorSize> make_boot_sector() {
    std::vector<std::uint8_t> code;

    // 16-bit x86 code loaded by BIOS at 0000:7C00.
    append(code, {
        0xFA,                         // cli
        0x31, 0xC0,                   // xor ax, ax
        0x8E, 0xD8,                   // mov ds, ax
        0x8E, 0xC0,                   // mov es, ax
        0x8E, 0xD0,                   // mov ss, ax
        0xBC, 0x00, 0x7C,             // mov sp, 0x7C00
        0xFB,                         // sti
        0xB8, 0x03, 0x00,             // mov ax, 0x0003 ; 80x25 text mode, clear screen
        0xCD, 0x10,                   // int 0x10
        0xBE, 0x22, 0x7C,             // mov si, message
        0xAC,                         // lodsb
        0x3C, 0x00,                   // cmp al, 0
        0x74, 0x06,                   // je hang
        0xB4, 0x0E,                   // mov ah, 0x0E ; teletype output
        0xCD, 0x10,                   // int 0x10
        0xEB, 0xF5,                   // jmp print_loop
        0xFA,                         // hang: cli
        0xF4,                         // hlt
        0xEB, 0xFC,                   // jmp hang
    });

    append_string(code,
        "OC booted!\r\n"
        "Hello from a tiny C++-built OS image.\r\n"
        "Next step: load a real kernel.\r\n");
    append_byte(code, 0x00);

    if (code.size() > kBootSignatureOffset) {
        throw std::runtime_error("boot sector code is too large");
    }

    std::array<std::uint8_t, kBootSectorSize> sector{};
    std::copy(code.begin(), code.end(), sector.begin());
    sector[kBootSignatureOffset] = 0x55;
    sector[kBootSignatureOffset + 1] = 0xAA;
    return sector;
}

void write_image(const std::filesystem::path& path) {
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }

    const auto sector = make_boot_sector();
    std::ofstream image(path, std::ios::binary);
    if (!image) {
        throw std::runtime_error("cannot open output image");
    }

    image.write(reinterpret_cast<const char*>(sector.data()), sector.size());
    if (!image) {
        throw std::runtime_error("cannot write output image");
    }
}
} // namespace

int main(int argc, char** argv) {
    const auto output = argc > 1 ? std::filesystem::path{argv[1]} : std::filesystem::path{"build/os.img"};

    try {
        write_image(output);
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }

    std::cout << "created bootable image: " << output.string() << '\n';
    return 0;
}
