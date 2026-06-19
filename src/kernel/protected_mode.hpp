#pragma once

namespace oc::kernel {

template <typename AssemblerT>
void emit_protected_mode_demo(AssemblerT& kernel) {
    kernel.label("enter_protected_mode");
    kernel.call("clear_screen");
    kernel.mov_si("pmode_text");
    kernel.call("print_string");
    kernel.bytes({0xFA});                         // cli: BIOS interrupts are not used after this point.
    kernel.lgdt("gdt_descriptor");               // lgdt [gdt_descriptor]
    kernel.bytes({
        0x0F, 0x20, 0xC0,             // mov eax, cr0
        0x66, 0x83, 0xC8, 0x01,       // or eax, 1 (set CR0.PE)
        0x0F, 0x22, 0xC0              // mov cr0, eax
    });
    kernel.far_jump(0x0008, "pm32_entry");        // reload CS with the 32-bit code selector.

    kernel.label("pm32_entry");
    kernel.bytes({
        0xB8, 0x10, 0x00, 0x00, 0x00, // mov eax, 0x10 (32-bit data selector)
        0x8E, 0xD8,                   // mov ds, ax
        0x8E, 0xC0,                   // mov es, ax
        0x8E, 0xE0,                   // mov fs, ax
        0x8E, 0xE8,                   // mov gs, ax
        0x8E, 0xD0,                   // mov ss, ax
        0xBC, 0x00, 0x00, 0x09, 0x00, // mov esp, 0x90000
        0xBF, 0x00, 0x80, 0x0B, 0x00  // mov edi, 0xB8000
    });
    kernel.mov_esi_abs32("pm32_message");
    kernel.label("pm32_loop");
    kernel.bytes({0xAC, 0x84, 0xC0});             // lodsb; test al, al
    kernel.jump_if_zero_short("pm32_halt");
    kernel.bytes({0xB4, 0x0A, 0x66, 0xAB});       // mov ah, 0x0A; stosw
    kernel.jump_short("pm32_loop");
    kernel.label("pm32_halt");
    kernel.bytes({0xF4});                         // hlt
    kernel.jump_short("pm32_halt");

    kernel.label("gdt");
    kernel.dword(0x00000000);                     // null descriptor low dword
    kernel.dword(0x00000000);                     // null descriptor high dword
    kernel.dword(0x0000FFFF);                     // 32-bit code: limit/base low
    kernel.dword(0x00CF9A00);                     // 32-bit code: access/flags/base high
    kernel.dword(0x0000FFFF);                     // 32-bit data: limit/base low
    kernel.dword(0x00CF9200);                     // 32-bit data: access/flags/base high
    kernel.label("gdt_descriptor");
    kernel.word(24 - 1);
    kernel.dword_abs("gdt");
}

} // namespace oc::kernel
