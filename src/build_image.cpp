#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace {
constexpr std::size_t kBootSectorSize = 512;
constexpr std::size_t kBootSignatureOffset = 510;
constexpr std::uint16_t kBiosLoadAddress = 0x7C00;

class BootAssembler {
public:
    void byte(std::uint8_t value) { code_.push_back(value); }

    void word(std::uint16_t value) {
        byte(static_cast<std::uint8_t>(value & 0xFF));
        byte(static_cast<std::uint8_t>((value >> 8) & 0xFF));
    }

    void bytes(std::initializer_list<std::uint8_t> values) {
        code_.insert(code_.end(), values.begin(), values.end());
    }

    void text(std::string_view value) {
        code_.insert(code_.end(), value.begin(), value.end());
        byte(0x00);
    }

    void label(std::string name) {
        const auto [_, inserted] = labels_.emplace(std::move(name), code_.size());
        if (!inserted) {
            throw std::runtime_error("duplicate label");
        }
    }

    void call(std::string label) {
        byte(0xE8);
        add_fixup(std::move(label), FixupKind::Rel16);
        word(0x0000);
    }

    void jump(std::string label) {
        byte(0xE9);
        add_fixup(std::move(label), FixupKind::Rel16);
        word(0x0000);
    }

    void jump_if_equal(std::string label) {
        bytes({0x0F, 0x84});
        add_fixup(std::move(label), FixupKind::Rel16);
        word(0x0000);
    }

    void mov_si(std::string label) {
        byte(0xBE);
        add_fixup(std::move(label), FixupKind::Abs16);
        word(0x0000);
    }

    std::array<std::uint8_t, kBootSectorSize> finish() {
        resolve_fixups();

        if (code_.size() > kBootSignatureOffset) {
            throw std::runtime_error("boot sector code is too large: " + std::to_string(code_.size()) + " bytes");
        }

        std::array<std::uint8_t, kBootSectorSize> sector{};
        std::copy(code_.begin(), code_.end(), sector.begin());
        sector[kBootSignatureOffset] = 0x55;
        sector[kBootSignatureOffset + 1] = 0xAA;
        return sector;
    }

private:
    enum class FixupKind { Abs16, Rel16 };

    struct Fixup {
        std::size_t offset;
        std::string label;
        FixupKind kind;
    };

    void add_fixup(std::string label, FixupKind kind) {
        fixups_.push_back(Fixup{code_.size(), std::move(label), kind});
    }

    void patch_word(std::size_t offset, std::uint16_t value) {
        code_.at(offset) = static_cast<std::uint8_t>(value & 0xFF);
        code_.at(offset + 1) = static_cast<std::uint8_t>((value >> 8) & 0xFF);
    }

    void resolve_fixups() {
        for (const auto& fixup : fixups_) {
            const auto label = labels_.find(fixup.label);
            if (label == labels_.end()) {
                throw std::runtime_error("unknown label: " + fixup.label);
            }

            const auto target = label->second;
            if (fixup.kind == FixupKind::Abs16) {
                patch_word(fixup.offset, static_cast<std::uint16_t>(kBiosLoadAddress + target));
                continue;
            }

            const auto next_instruction = fixup.offset + 2;
            const auto relative = static_cast<std::int32_t>(target) - static_cast<std::int32_t>(next_instruction);
            if (relative < -32768 || relative > 32767) {
                throw std::runtime_error("relative jump is out of range");
            }
            patch_word(fixup.offset, static_cast<std::uint16_t>(static_cast<std::int16_t>(relative)));
        }
    }

    std::vector<std::uint8_t> code_;
    std::unordered_map<std::string, std::size_t> labels_;
    std::vector<Fixup> fixups_;
};

std::array<std::uint8_t, kBootSectorSize> make_boot_sector() {
    BootAssembler boot;

    // 16-bit x86 code loaded by BIOS at 0000:7C00.
    boot.bytes({
        0xFA,                         // cli
        0x31, 0xC0,                   // xor ax, ax
        0x8E, 0xD8,                   // mov ds, ax
        0x8E, 0xC0,                   // mov es, ax
        0x8E, 0xD0,                   // mov ss, ax
        0xBC, 0x00, 0x7C,             // mov sp, 0x7C00
        0xFB,                         // sti
    });
    boot.jump("draw_home");

    boot.label("main_loop");
    boot.bytes({0x31, 0xC0, 0xCD, 0x16});         // xor ax, ax; int 0x16 (wait key)
    boot.bytes({0x3C, static_cast<std::uint8_t>('h')});
    boot.jump_if_equal("show_help");
    boot.bytes({0x3C, static_cast<std::uint8_t>('H')});
    boot.jump_if_equal("show_help");
    boot.bytes({0x3C, static_cast<std::uint8_t>('a')});
    boot.jump_if_equal("show_about");
    boot.bytes({0x3C, static_cast<std::uint8_t>('A')});
    boot.jump_if_equal("show_about");
    boot.bytes({0x3C, static_cast<std::uint8_t>('c')});
    boot.jump_if_equal("draw_home");
    boot.bytes({0x3C, static_cast<std::uint8_t>('C')});
    boot.jump_if_equal("draw_home");
    boot.bytes({0x3C, static_cast<std::uint8_t>('r')});
    boot.jump_if_equal("reboot");
    boot.bytes({0x3C, static_cast<std::uint8_t>('R')});
    boot.jump_if_equal("reboot");
    boot.jump("main_loop");

    boot.label("draw_home");
    boot.call("clear_screen");
    boot.mov_si("home_text");
    boot.call("print_string");
    boot.jump("main_loop");

    boot.label("show_help");
    boot.call("clear_screen");
    boot.mov_si("help_text");
    boot.call("print_string");
    boot.jump("main_loop");

    boot.label("show_about");
    boot.call("clear_screen");
    boot.mov_si("about_text");
    boot.call("print_string");
    boot.jump("main_loop");

    boot.label("reboot");
    boot.bytes({0xCD, 0x19});                         // int 0x19 (bootstrap loader)
    boot.jump("reboot");

    boot.label("clear_screen");
    boot.bytes({0xB8, 0x03, 0x00, 0xCD, 0x10, 0xC3}); // mov ax, 0x0003; int 0x10; ret

    boot.label("print_string");
    boot.bytes({0xAC});                               // lodsb
    boot.bytes({0x3C, 0x00});                         // cmp al, 0
    boot.jump_if_equal("print_done");
    boot.bytes({0xB4, 0x0E, 0xBB, 0x07, 0x00, 0xCD, 0x10}); // BIOS teletype, page 0, light gray
    boot.jump("print_string");
    boot.label("print_done");
    boot.bytes({0xC3});                               // ret

    boot.label("home_text");
    boot.text(
        "OC mini shell\r\n"
        "------------\r\n"
        "H help  A about\r\n"
        "C clear R reboot\r\n\r\n"
        "Press a key...\r\n");

    boot.label("help_text");
    boot.text(
        "Help\r\n"
        "H: show this screen\r\n"
        "A: about this OS\r\n"
        "C: clear screen\r\n"
        "R: reboot VM\r\n");

    boot.label("about_text");
    boot.text(
        "About OC\r\n"
        "Tiny BIOS OS starter.\r\n"
        "Built by C++ image builder.\r\n"
        "Next: load a kernel.\r\n");

    return boot.finish();
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
