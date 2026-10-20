# Blackbit kernel

A freestanding kernel with a 32-bit x86 port and a 64-bit x86 port. The x32 primary boot path is a two-stage assembly BIOS loader that reads a raw disk image, gathers an E820 memory map, and passes a Multiboot2-compatible handoff. The x64 port enters long mode from a Multiboot2 GRUB handoff. Both x86 ports build the same command kernel with VGA text output, polled PS/2 keyboard input, a small RAM-only virtual filesystem, CPU/RAM information, and Kaya host-bridge commands. Separate ARM64 and ARM32 assembly bootstrap payloads are included; they only initialize PL011 serial output and halt, and are not ports of the command kernel.

## Build and run (WSL2 Ubuntu)

Install `gcc`, `make`, `binutils`, `grub-pc-bin`, `grub-common`, `xorriso`, `qemu-system-x86`, and `coreutils` (for `timeout`). Then:

```sh
cd kernel
make          # builds x32 UI/shell ISO images, x64 ISO, and x32 BIOS disk image
make iso-x64  # builds the x64 GRUB ISO
make test     # boots a test build in QEMU and runs kernel command checks
make test-x64 # boots the x64 build in QEMU and runs the same command checks
make run      # builds Kaya and launches the BIOS image with its serial bridge
```

The primary x32 boot image is `build/blackbit-bios.img` (16 MiB); setup data is stored at LBA 2048, away from boot stages and the kernel. x32 GRUB images are `build/blackbit-ui.iso` and `build/blackbit-shell.iso`; the x64 GRUB image is `build/blackbit-x64.iso`. `make test` and `make test-x64` exit nonzero if any command check fails. Both QEMU self-tests currently pass 90 checks.

`bash build-deliverables.sh` writes the requested 8 files to `C:\Users\srhtm\Downloads\biso` from WSL. It requires `aarch64-linux-gnu-{as,ld,objcopy}` and `arm-linux-gnueabihf-{as,ld,objcopy}` for ARM outputs. The x32 and x64 ISO/IMG files are bootable; ARM `.img` files are bootstrap kernel payloads, and ARM `.iso` files are data bundles because this environment has no ARM EFI GRUB target or ARM QEMU runtime. See the output folder's README before using those experimental files.

## Kaya integration

The imported Kaya snapshot is in `../Kaya/` (upstream commit `5f22988`) and contains 200 `.kay` packages. `make run` builds its POSIX development manager and forwards `forge` requests over a restricted QEMU serial bridge. A native kernel-side Kaya runtime is not yet implemented.

## Commands

| Command | Behavior in this build |
| --- | --- |
| `peek <file>` | Read a file (`show`, `cat`, `v` also work). |
| `scribe <file> <text...>` | Write a text line to a RAM file. |
| `spawn <file>`, `vault <dir>` | Create a file or directory (`n`, `nest` are aliases). |
| `clone <src> <dst>`, `shift <src> <dst>`, `erase <file>` | Copy, move/rename, or remove a RAM file (`c`, `m`, `d` aliases). |
| `wipe <file>` | Zero the file's RAM buffer, then remove it. Bare `wipe` clears the display. |
| `seal <file>`, `unseal <file>` | Prevent/allow file writes and deletion. |
| `enter <dir>`, `leave`, `where`, `treeview <dir>` | Navigate and inspect the RAM filesystem. |
| `collect <dir>`, `gather <fragment>`, `sweep <empty-dir>` | List entries, search names, remove an empty directory. |
| `machine`, `kernelinfo`, `uptime`, `health`, `ramview`, `coreview` | Show the kernel identity, runtime, memory, CPU, and implemented subsystem status. |
| `tasks` | Show the kernel console task; there is no general process scheduler yet. |
| `forge list`, `forge install/remove <package>`, `forge update`, `forge upgrade` | Forward supported Kaya package operations over the existing WSL serial bridge. `forge search` reports that repository search is not exposed by the bridge yet. |
| `p`, `v`, `r`, `x`, `d`, `m`, `c`, `n` | Short aliases for print, view, run, execute, delete, move, copy, and new-file operations. |

Network, user-account, process-control, and general disk commands are recognized but report `unavailable` until their corresponding kernel subsystem and drivers exist. Kaya/WSL package operations are host bridge requests, not native kernel processes. The virtual files created by the current kernel live in RAM and disappear after reboot; only the setup record is persisted by the existing QEMU data-disk path.
