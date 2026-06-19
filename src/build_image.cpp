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
constexpr std::size_t kSectorSize = 512;
constexpr std::size_t kBootSignatureOffset = 510;
constexpr std::uint16_t kBootLoadAddress = 0x7C00;
constexpr std::uint16_t kKernelLoadAddress = 0x8000;

class Assembler {
public:
    explicit Assembler(std::uint16_t origin) : origin_(origin) {}

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

    void jump_if_carry(std::string label) {
        jump_if_below(std::move(label));
    }

    void jump_if_below(std::string label) {
        bytes({0x0F, 0x82});
        add_fixup(std::move(label), FixupKind::Rel16);
        word(0x0000);
    }

    void mov_si(std::string label) {
        byte(0xBE);
        add_fixup(std::move(label), FixupKind::Abs16);
        word(0x0000);
    }

    void mov_mem8_dl(std::string label) {
        bytes({0x88, 0x16});
        add_fixup(std::move(label), FixupKind::Abs16);
        word(0x0000);
    }

    void mov_mem16_imm(std::string label, std::uint16_t value) {
        bytes({0xC7, 0x06});
        add_fixup(std::move(label), FixupKind::Abs16);
        word(0x0000);
        word(value);
    }

    void mov_di_mem16(std::string label) {
        bytes({0x8B, 0x3E});
        add_fixup(std::move(label), FixupKind::Abs16);
        word(0x0000);
    }

    void mov_ax_mem16(std::string label) {
        byte(0xA1);
        add_fixup(std::move(label), FixupKind::Abs16);
        word(0x0000);
    }

    void mov_mem16_di(std::string label) {
        bytes({0x89, 0x3E});
        add_fixup(std::move(label), FixupKind::Abs16);
        word(0x0000);
    }

    void add_mem16_bx(std::string label) {
        bytes({0x01, 0x1E});
        add_fixup(std::move(label), FixupKind::Abs16);
        word(0x0000);
    }

    void mov_al_mem8(std::string label) {
        byte(0xA0);
        add_fixup(std::move(label), FixupKind::Abs16);
        word(0x0000);
    }

    void mov_ah_mem8(std::string label) {
        bytes({0x8A, 0x26});
        add_fixup(std::move(label), FixupKind::Abs16);
        word(0x0000);
    }

    void mov_mem8_al(std::string label) {
        byte(0xA2);
        add_fixup(std::move(label), FixupKind::Abs16);
        word(0x0000);
    }

    std::vector<std::uint8_t> finish() {
        resolve_fixups();
        return code_;
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
                patch_word(fixup.offset, static_cast<std::uint16_t>(origin_ + target));
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

    std::uint16_t origin_;
    std::vector<std::uint8_t> code_;
    std::unordered_map<std::string, std::size_t> labels_;
    std::vector<Fixup> fixups_;
};

void append_sector_padding(std::vector<std::uint8_t>& bytes) {
    const auto remainder = bytes.size() % kSectorSize;
    if (remainder != 0) {
        bytes.resize(bytes.size() + (kSectorSize - remainder), 0x00);
    }
}

std::array<std::uint8_t, kSectorSize> make_boot_sector(std::uint8_t kernel_sector_count) {
    if (kernel_sector_count == 0) {
        throw std::runtime_error("kernel must occupy at least one sector");
    }

    Assembler boot(kBootLoadAddress);
    boot.bytes({
        0xFA,                         // cli
        0x31, 0xC0,                   // xor ax, ax
        0x8E, 0xD8,                   // mov ds, ax
        0x8E, 0xC0,                   // mov es, ax
        0x8E, 0xD0,                   // mov ss, ax
        0xBC, 0x00, 0x7C,             // mov sp, 0x7C00
        0xFB,                         // sti
    });
    boot.mov_mem8_dl("boot_drive");
    boot.mov_si("boot_drive");
    boot.bytes({
        0xBB, 0x00, 0x80,             // mov bx, 0x8000
        0xB4, 0x02,                   // mov ah, 0x02 (read sectors)
        0xB0, kernel_sector_count,    // mov al, kernel_sector_count
        0xB5, 0x00,                   // mov ch, 0 (cylinder)
        0xB1, 0x02,                   // mov cl, 2 (first sector after boot sector)
        0xB6, 0x00,                   // mov dh, 0 (head)
        0x8A, 0x14,                   // mov dl, [si]
        0xCD, 0x13                    // int 0x13
    });
    boot.jump_if_carry("disk_error");
    boot.bytes({0xEA, 0x00, 0x80, 0x00, 0x00});   // jmp 0000:8000

    boot.label("disk_error");
    boot.mov_si("error_text");
    boot.call("print_string");
    boot.bytes({0xCD, 0x16, 0xCD, 0x19});         // wait for key; reboot

    boot.label("print_string");
    boot.bytes({0xAC, 0x3C, 0x00});               // lodsb; cmp al, 0
    boot.jump_if_equal("print_done");
    boot.bytes({0xB4, 0x0E, 0xBB, 0x07, 0x00, 0xCD, 0x10});
    boot.jump("print_string");
    boot.label("print_done");
    boot.bytes({0xC3});

    boot.label("boot_drive");
    boot.byte(0x00);
    boot.label("error_text");
    boot.text("OC boot: disk read error. Press key.\r\n");

    auto bytes = boot.finish();
    if (bytes.size() > kBootSignatureOffset) {
        throw std::runtime_error("boot sector code is too large: " + std::to_string(bytes.size()) + " bytes");
    }

    std::array<std::uint8_t, kSectorSize> sector{};
    std::copy(bytes.begin(), bytes.end(), sector.begin());
    sector[kBootSignatureOffset] = 0x55;
    sector[kBootSignatureOffset + 1] = 0xAA;
    return sector;
}

std::vector<std::uint8_t> make_kernel() {
    Assembler kernel(kKernelLoadAddress);

    kernel.bytes({
        0xFA,                         // cli
        0x31, 0xC0,                   // xor ax, ax
        0x8E, 0xD8,                   // mov ds, ax
        0x8E, 0xC0,                   // mov es, ax
        0x8E, 0xD0,                   // mov ss, ax
        0xBC, 0x00, 0x80,             // mov sp, 0x8000
        0xFB,                         // sti
    });
    kernel.jump("draw_home");

    kernel.label("main_loop");
    kernel.bytes({0x31, 0xC0, 0xCD, 0x16});       // wait key
    kernel.bytes({0x24, 0xDF});                   // uppercase ASCII letter
    const std::vector<std::pair<char, std::string>> commands = {
        {'H', "show_help"}, {'A', "show_about"}, {'C', "draw_home"}, {'R', "reboot"},
        {'E', "echo_mode"}, {'B', "beep"}, {'M', "show_memory"}, {'K', "show_keyboard"},
        {'D', "show_disk"}, {'T', "show_tasks"}, {'V', "show_video"}, {'S', "show_syscalls"},
        {'N', "show_notes"}, {'P', "show_processes"}, {'F', "cycle_color"}, {'G', "position_demo"},
    };
    for (const auto& [key, label] : commands) {
        kernel.bytes({0x3C, static_cast<std::uint8_t>(key)});
        kernel.jump_if_equal(label);
    }
    kernel.jump("main_loop");

    auto screen = [&kernel](std::string_view label, std::string_view text_label) {
        kernel.label(std::string(label));
        kernel.call("clear_screen");
        kernel.mov_si(std::string(text_label));
        kernel.call("print_string");
        kernel.jump("main_loop");
    };

    kernel.label("draw_home");
    kernel.call("clear_screen");
    kernel.mov_si("home_text");
    kernel.call("print_string");
    kernel.jump("main_loop");

    screen("show_help", "help_text");
    screen("show_about", "about_text");
    screen("show_memory", "memory_text");
    screen("show_keyboard", "keyboard_text");
    screen("show_disk", "disk_text");
    screen("show_tasks", "tasks_text");
    screen("show_video", "video_text");
    screen("show_syscalls", "syscalls_text");
    screen("show_notes", "notes_text");
    screen("show_processes", "processes_text");

    kernel.label("cycle_color");
    kernel.mov_al_mem8("text_attr");
    kernel.bytes({
        0xFE, 0xC0,                   // inc al
        0x24, 0x0F,                   // and al, 0x0F
        0x3C, 0x00                    // cmp al, 0
    });
    kernel.jump_if_equal("color_nonzero");
    kernel.jump("store_color");
    kernel.label("color_nonzero");
    kernel.bytes({0xB0, 0x01});       // mov al, 1
    kernel.label("store_color");
    kernel.mov_mem8_al("text_attr");
    kernel.call("clear_screen");
    kernel.mov_si("color_text");
    kernel.call("print_string");
    kernel.jump("main_loop");

    kernel.label("position_demo");
    kernel.call("clear_screen");
    kernel.mov_mem16_imm("cursor_pos", 0x07B2);   // row 12, column 25
    kernel.call("normalize_cursor");
    kernel.mov_si("position_text");
    kernel.call("print_string");
    kernel.jump("main_loop");

    kernel.label("beep");
    kernel.bytes({0xB0, 0x07, 0xB4, 0x0E, 0xBB, 0x07, 0x00, 0xCD, 0x10});
    kernel.jump("main_loop");

    kernel.label("echo_mode");
    kernel.call("clear_screen");
    kernel.mov_si("echo_text");
    kernel.call("print_string");
    kernel.label("echo_loop");
    kernel.bytes({0x31, 0xC0, 0xCD, 0x16});       // wait key
    kernel.bytes({0x3C, 0x1B});                   // Esc exits
    kernel.jump_if_equal("draw_home");
    kernel.call("print_char");
    kernel.jump("echo_loop");

    kernel.label("reboot");
    kernel.bytes({0xCD, 0x19});
    kernel.jump("reboot");

    kernel.label("clear_screen");
    kernel.bytes({
        0x50,                         // push ax
        0x51,                         // push cx
        0x57,                         // push di
        0x06,                         // push es
        0xB8, 0x00, 0xB8,             // mov ax, 0xB800
        0x8E, 0xC0,                   // mov es, ax
        0x31, 0xFF,                   // xor di, di
        0xB8, 0x20, 0x07,             // mov ax, 0x0720
        0xB9, 0xD0, 0x07,             // mov cx, 2000
        0xF3, 0xAB                    // rep stosw
    });
    kernel.mov_mem16_imm("cursor_pos", 0x0000);
    kernel.call("normalize_cursor");
    kernel.bytes({0x07, 0x5F, 0x59, 0x58, 0xC3}); // pop es; pop di; pop cx; pop ax; ret

    kernel.label("print_string");
    kernel.bytes({0xAC, 0x3C, 0x00});             // lodsb; cmp al, 0
    kernel.jump_if_equal("print_done");
    kernel.call("print_char");
    kernel.jump("print_string");
    kernel.label("print_done");
    kernel.bytes({0xC3});

    kernel.label("print_char");
    kernel.bytes({0x3C, 0x0D});                   // cmp al, CR
    kernel.jump_if_equal("char_done");
    kernel.bytes({0x3C, 0x0A});                   // cmp al, LF
    kernel.jump_if_equal("newline");
    kernel.bytes({0x3C, 0x08});                   // cmp al, Backspace
    kernel.jump_if_equal("backspace");
    kernel.bytes({0x50, 0x53, 0x57, 0x06});       // push ax; push bx; push di; push es
    kernel.bytes({0xBB, 0x00, 0xB8, 0x8E, 0xC3}); // mov bx, 0xB800; mov es, bx
    kernel.mov_di_mem16("cursor_pos");
    kernel.mov_ah_mem8("text_attr");
    kernel.bytes({0xAB});                         // stosw
    kernel.mov_mem16_di("cursor_pos");
    kernel.call("normalize_cursor");
    kernel.bytes({0x07, 0x5F, 0x5B, 0x58});       // pop es; pop di; pop bx; pop ax
    kernel.label("char_done");
    kernel.bytes({0xC3});

    kernel.label("newline");
    kernel.bytes({0x50, 0x53, 0x52});             // push ax; push bx; push dx
    kernel.mov_ax_mem16("cursor_pos");
    kernel.bytes({
        0x31, 0xD2,                   // xor dx, dx
        0xBB, 0xA0, 0x00,             // mov bx, 160
        0xF7, 0xF3,                   // div bx
        0x29, 0xD3                    // sub bx, dx
    });
    kernel.add_mem16_bx("cursor_pos");
    kernel.call("normalize_cursor");
    kernel.bytes({0x5A, 0x5B, 0x58, 0xC3});       // pop dx; pop bx; pop ax; ret

    kernel.label("backspace");
    kernel.bytes({0x50, 0x57, 0x06});             // push ax; push di; push es
    kernel.mov_di_mem16("cursor_pos");
    kernel.bytes({0x81, 0xFF, 0x00, 0x00});       // cmp di, 0
    kernel.jump_if_equal("backspace_done");
    kernel.bytes({0x83, 0xEF, 0x02});             // sub di, 2
    kernel.mov_mem16_di("cursor_pos");
    kernel.bytes({
        0xB8, 0x00, 0xB8,             // mov ax, 0xB800
        0x8E, 0xC0,                   // mov es, ax
        0xB8, 0x20, 0x07,             // mov ax, 0x0720
        0xAB                          // stosw
    });
    kernel.call("normalize_cursor");
    kernel.label("backspace_done");
    kernel.bytes({0x07, 0x5F, 0x58, 0xC3});       // pop es; pop di; pop ax; ret

    kernel.label("cursor_pos");
    kernel.word(0x0000);
    kernel.label("text_attr");
    kernel.byte(0x07);

    kernel.label("normalize_cursor");
    kernel.bytes({0x50, 0x53, 0x51, 0x52, 0x56, 0x57, 0x1E, 0x06}); // push ax,bx,cx,dx,si,di,ds,es
    kernel.mov_ax_mem16("cursor_pos");
    kernel.bytes({0x3D, 0xA0, 0x0F});             // cmp ax, 4000
    kernel.jump_if_below("update_hw_cursor");
    kernel.bytes({
        0xB8, 0x00, 0xB8,             // mov ax, 0xB800
        0x8E, 0xD8,                   // mov ds, ax
        0x8E, 0xC0,                   // mov es, ax
        0xBE, 0xA0, 0x00,             // mov si, 160
        0x31, 0xFF,                   // xor di, di
        0xB9, 0x80, 0x07,             // mov cx, 1920
        0xF3, 0xA5,                   // rep movsw
        0xB8, 0x20, 0x07,             // mov ax, 0x0720
        0xB9, 0x50, 0x00,             // mov cx, 80
        0xF3, 0xAB,                   // rep stosw
        0x31, 0xC0,                   // xor ax, ax
        0x8E, 0xD8                    // mov ds, ax (restore kernel data segment for labels)
    });
    kernel.mov_mem16_imm("cursor_pos", 0x0F00);   // start of last text row

    kernel.label("update_hw_cursor");
    kernel.mov_ax_mem16("cursor_pos");
    kernel.bytes({
        0xD1, 0xE8,                   // shr ax, 1 (word offset -> cell offset)
        0x89, 0xC3,                   // mov bx, ax
        0xBA, 0xD4, 0x03,             // mov dx, 0x03D4
        0xB0, 0x0F,                   // mov al, 0x0F
        0xEE,                         // out dx, al
        0x42,                         // inc dx
        0x88, 0xD8,                   // mov al, bl
        0xEE,                         // out dx, al
        0x4A,                         // dec dx
        0xB0, 0x0E,                   // mov al, 0x0E
        0xEE,                         // out dx, al
        0x42,                         // inc dx
        0x88, 0xF8,                   // mov al, bh
        0xEE,                         // out dx, al
        0x07, 0x1F, 0x5F, 0x5E, 0x5A, 0x59, 0x5B, 0x58, 0xC3
                                      // pop es,ds,di,si,dx,cx,bx,ax; ret
    });

    kernel.label("home_text");
    kernel.text(
        "OC kernel shell\r\n"
        "===============\r\n"
        "H help A about C home R reboot\r\n"
        "E echo B beep M memory K keyboard\r\n"
        "D disk T tasks V video S syscalls\r\n"
        "N notes P processes F color G goto\r\n\r\n"
        "Press a key...\r\n");

    kernel.label("help_text");
    kernel.text(
        "Help\r\n"
        "H: this help\r\n"
        "A: kernel info\r\n"
        "C: redraw desktop\r\n"
        "R: BIOS reboot\r\n"
        "E: echo typed keys until Esc\r\n"
        "B: terminal bell\r\n"
        "F: cycle text color\r\n"
        "G: positioned text demo\r\n"
        "M/K/D/T/V/S/N/P: kernel info panels\r\n");

    kernel.label("about_text");
    kernel.text(
        "About OC kernel\r\n"
        "Boot sector loads this kernel from disk.\r\n"
        "Kernel lives at 0000:8000 and owns the shell.\r\n"
        "Still real mode, but no longer only boot code.\r\n");

    kernel.label("memory_text");
    kernel.text(
        "Memory map v0\r\n"
        "Boot sector: 0000:7C00\r\n"
        "Kernel:      0000:8000\r\n"
        "Stack:       grows below 8000\r\n"
        "Text VRAM:   B800:0000\r\n");

    kernel.label("keyboard_text");
    kernel.text(
        "Keyboard driver v0\r\n"
        "BIOS int 16h waits for a key.\r\n"
        "ASCII commands are case-insensitive.\r\n"
        "Echo mode returns with Esc.\r\n");

    kernel.label("disk_text");
    kernel.text(
        "Disk loader v0\r\n"
        "Boot uses BIOS int 13h.\r\n"
        "It reads kernel sectors starting at sector 2.\r\n"
        "This image now has bootloader + kernel.\r\n");

    kernel.label("tasks_text");
    kernel.text(
        "Task manager stub\r\n"
        "PID 0: idle shell\r\n"
        "PID 1: keyboard loop\r\n"
        "Next: timer IRQ and scheduler.\r\n");

    kernel.label("video_text");
    kernel.text(
        "Video console v0\r\n"
        "Mode: BIOS text 80x25\r\n"
        "Output: direct writes to B800 memory.\r\n"
        "Backspace erases echoed text.\r\n"
        "Screen scrolls and hardware cursor moves.\r\n"
        "F cycles color, G positions text.\r\n");

    kernel.label("color_text");
    kernel.text(
        "Color changed\r\n"
        "New text uses the next VGA foreground color.\r\n"
        "Press F again to keep cycling.\r\n");

    kernel.label("position_text");
    kernel.text(
        "Positioned text demo\r\n"
        "This message starts near the middle of the screen.\r\n"
        "Direct VRAM output can place text anywhere.\r\n");

    kernel.label("syscalls_text");
    kernel.text(
        "Syscall plan\r\n"
        "0 print string\r\n"
        "1 read key\r\n"
        "2 clear screen\r\n"
        "Next: software interrupt API.\r\n");

    kernel.label("echo_text");
    kernel.text(
        "Echo mode\r\n"
        "Type anything; Esc returns to the kernel menu.\r\n\r\n");

    kernel.label("notes_text");
    kernel.text(
        "Kernel notes\r\n"
        "Separate kernel means more than 512 bytes.\r\n"
        "Next milestones: protected mode, C++ kernel, drivers.\r\n");

    kernel.label("processes_text");
    kernel.text(
        "Processes v0\r\n"
        "No isolation yet.\r\n"
        "Shell is the first userspace-like task.\r\n"
        "Next: process table in RAM.\r\n");

    auto bytes = kernel.finish();
    append_sector_padding(bytes);
    return bytes;
}

std::vector<std::uint8_t> make_image() {
    auto kernel = make_kernel();
    const auto kernel_sector_count = kernel.size() / kSectorSize;
    if (kernel_sector_count > 127) {
        throw std::runtime_error("kernel is too large for one BIOS read");
    }

    const auto boot = make_boot_sector(static_cast<std::uint8_t>(kernel_sector_count));
    std::vector<std::uint8_t> image;
    image.insert(image.end(), boot.begin(), boot.end());
    image.insert(image.end(), kernel.begin(), kernel.end());
    return image;
}

void write_image(const std::filesystem::path& path) {
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }

    const auto image_bytes = make_image();
    std::ofstream image(path, std::ios::binary);
    if (!image) {
        throw std::runtime_error("cannot open output image");
    }

    image.write(reinterpret_cast<const char*>(image_bytes.data()), static_cast<std::streamsize>(image_bytes.size()));
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
