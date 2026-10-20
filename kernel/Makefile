CC := gcc
AS := gcc
LD := ld
CFLAGS := -m32 -std=gnu11 -O2 -Wall -Wextra -ffreestanding -fno-stack-protector -fno-pic -fno-pie -fno-builtin -fno-asynchronous-unwind-tables -fno-unwind-tables
LDFLAGS := -m elf_i386 -T linker.ld -nostdlib
BUILD := build

.PHONY: all iso run clean shell ui test
all: iso

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/boot.o: boot.S | $(BUILD)
	$(AS) $(CFLAGS) -c $< -o $@

$(BUILD)/kernel-ui.o: kernel.c | $(BUILD)
	$(CC) $(CFLAGS) -DBLACKBIT_UI=1 -c $< -o $@

$(BUILD)/kernel-shell.o: kernel.c | $(BUILD)
	$(CC) $(CFLAGS) -DBLACKBIT_UI=0 -c $< -o $@

$(BUILD)/kernel-ui.elf: $(BUILD)/boot.o $(BUILD)/kernel-ui.o
	$(LD) $(LDFLAGS) -o $@ $^

$(BUILD)/kernel-shell.elf: $(BUILD)/boot.o $(BUILD)/kernel-shell.o
	$(LD) $(LDFLAGS) -o $@ $^

$(BUILD)/iso-ui/boot/grub/grub.cfg: boot/grub/grub.cfg | $(BUILD)
	mkdir -p $(BUILD)/iso-ui/boot/grub
	cp $< $@

$(BUILD)/iso-shell/boot/grub/grub.cfg: boot/grub/grub.cfg | $(BUILD)
	mkdir -p $(BUILD)/iso-shell/boot/grub
	cp $< $@

ui: $(BUILD)/kernel-ui.elf $(BUILD)/iso-ui/boot/grub/grub.cfg
	cp $(BUILD)/kernel-ui.elf $(BUILD)/iso-ui/boot/kernel.elf
	grub-mkrescue -o $(BUILD)/blackbit-ui.iso $(BUILD)/iso-ui

shell: $(BUILD)/kernel-shell.elf $(BUILD)/iso-shell/boot/grub/grub.cfg
	cp $(BUILD)/kernel-shell.elf $(BUILD)/iso-shell/boot/kernel.elf
	grub-mkrescue -o $(BUILD)/blackbit-shell.iso $(BUILD)/iso-shell

iso: ui shell

run: ui
	qemu-system-i386 -cdrom $(BUILD)/blackbit-ui.iso -drive file=$(BUILD)/blackbit-data.img,format=raw,if=ide,index=0 -m 128M -no-reboot

clean:
	rm -rf $(BUILD)

# Run command-level checks inside the real kernel under QEMU without setup input.
$(BUILD)/kernel-test.o: kernel.c | $(BUILD)
	$(CC) $(CFLAGS) -Wno-unused-function -DBLACKBIT_UI=0 -DBLACKBIT_SELFTEST=1 -c $< -o $@

$(BUILD)/kernel-test.elf: $(BUILD)/boot.o $(BUILD)/kernel-test.o
	$(LD) $(LDFLAGS) -o $@ $^

$(BUILD)/iso-test/boot/grub/grub.cfg: boot/grub/grub.cfg | $(BUILD)
	mkdir -p $(BUILD)/iso-test/boot/grub
	cp $< $@

$(BUILD)/blackbit-test.iso: $(BUILD)/kernel-test.elf $(BUILD)/iso-test/boot/grub/grub.cfg
	cp $(BUILD)/kernel-test.elf $(BUILD)/iso-test/boot/kernel.elf
	grub-mkrescue -o $@ $(BUILD)/iso-test

test: $(BUILD)/blackbit-test.iso
	@set +e; timeout 5s qemu-system-i386 -cdrom $(BUILD)/blackbit-test.iso -m 128M -display none -serial stdio -monitor none -no-reboot -device isa-debug-exit,iobase=0xf4,iosize=0x04 >$(BUILD)/selftest.log 2>&1; rc=$$?; set -e; cat $(BUILD)/selftest.log; test $$rc -eq 33; grep -q "SELFTEST: .* passed, 0 failed" $(BUILD)/selftest.log
