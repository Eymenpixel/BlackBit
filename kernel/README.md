# Blackbit kernel

A freestanding x86 protected-mode kernel booted by GRUB Multiboot2. The kernel currently provides a VGA text console, polled PS/2 keyboard input, a small RAM-only virtual filesystem, basic CPU/RAM/RTC information, and a command interpreter. This is an early kernel foundation: it does not yet implement a scheduler, user mode, network stack, general block-device/filesystem drivers, ACPI, or account authorization.

## Build and run (WSL2 Ubuntu)

Install `gcc`, `make`, `binutils`, `grub-pc-bin`, `grub-common`, `xorriso`, `qemu-system-x86`, and `coreutils` (for `timeout`). Then:

```sh
cd kernel
make          # builds UI and shell ISO images
make test     # boots a test build in QEMU and runs kernel command checks
make run      # launches the UI edition in QEMU
```

The output images are `build/blackbit-ui.iso` and `build/blackbit-shell.iso`. `make test` exits nonzero if a command check fails. The self-test checks the real kernel command dispatcher under QEMU; commands needing missing hardware subsystems are checked to report their unavailability clearly.

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
