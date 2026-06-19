# Kernel source notes

The current kernel is still emitted by the C++ image builder, but protected-mode code is split out here so the architecture-specific transition is visible and reviewable.

## Protected mode status

- `protected_mode.hpp` emits the GDT, `lgdt`, `CR0.PE` enable sequence, and the far jump into a 32-bit code segment.
- The existing interactive shell intentionally remains a **real-mode shell** for now because it still uses BIOS services such as `int 16h` for keyboard input and `int 19h` for reboot.
- The `X` command is therefore a one-way protected-mode demo: after switching, it does not call BIOS interrupts. It writes directly to `0xB8000` and halts.

Next step: move the actual shell loop to protected mode by replacing BIOS keyboard/reboot calls with protected-mode drivers/interrupt handlers.
