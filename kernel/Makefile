CC := gcc
AS := gcc
LD := ld
CFLAGS := -m32 -std=gnu11 -O2 -Wall -Wextra -ffreestanding -fno-stack-protector -fno-pic -fno-pie -fno-builtin -fno-asynchronous-unwind-tables -fno-unwind-tables
LDFLAGS := -m elf_i386 -T linker.ld -nostdlib
BUILD := build

.PHONY: all iso run clean shell ui test boot-image run-bios kaya
all: iso boot-image

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

run: boot-image kaya
	python3 ../tools/kaya_bridge.py

kaya:
	$(MAKE) -C ../Kaya/Kaya all

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

$(BUILD)/stage1.elf: bootloader/stage1.S bootloader/stage1.ld | $(BUILD)
	$(CC) -m32 -c $< -o $(BUILD)/stage1.o
	$(LD) -m elf_i386 -T bootloader/stage1.ld -o $@ $(BUILD)/stage1.o

$(BUILD)/stage1.bin: $(BUILD)/stage1.elf
	objcopy -O binary $< $@

$(BUILD)/kernel-ui.bin: $(BUILD)/kernel-ui.elf
	objcopy -O binary $< $@

$(BUILD)/kernel-shell.bin: $(BUILD)/kernel-shell.elf
	objcopy -O binary $< $@

$(BUILD)/kernel-test.bin: $(BUILD)/kernel-test.elf
	objcopy -O binary $< $@

$(BUILD)/stage2-ui.o: bootloader/stage2.S $(BUILD)/kernel-ui.bin
	$(CC) -m32 -DKERNEL_BYTES=$$(stat -c%s $(BUILD)/kernel-ui.bin) -DKERNEL_SECTORS=$$(( ($$(stat -c%s $(BUILD)/kernel-ui.bin) + 511) / 512 )) -c $< -o $@

$(BUILD)/stage2-test.o: bootloader/stage2.S $(BUILD)/kernel-test.bin
	$(CC) -m32 -DKERNEL_BYTES=$$(stat -c%s $(BUILD)/kernel-test.bin) -DKERNEL_SECTORS=$$(( ($$(stat -c%s $(BUILD)/kernel-test.bin) + 511) / 512 )) -c $< -o $@

$(BUILD)/stage2-ui.elf: $(BUILD)/stage2-ui.o bootloader/stage2.ld
	$(LD) -m elf_i386 -T bootloader/stage2.ld -o $@ $(BUILD)/stage2-ui.o

$(BUILD)/stage2-test.elf: $(BUILD)/stage2-test.o bootloader/stage2.ld
	$(LD) -m elf_i386 -T bootloader/stage2.ld -o $@ $(BUILD)/stage2-test.o

$(BUILD)/stage2-ui.bin: $(BUILD)/stage2-ui.elf
	objcopy -O binary $< $@

$(BUILD)/stage2-test.bin: $(BUILD)/stage2-test.elf
	objcopy -O binary $< $@

$(BUILD)/blackbit-bios.img: $(BUILD)/stage1.bin $(BUILD)/stage2-ui.bin $(BUILD)/kernel-ui.bin bootloader/make_image.py
	python3 bootloader/make_image.py $(BUILD)/stage1.bin $(BUILD)/stage2-ui.bin $(BUILD)/kernel-ui.bin $@

$(BUILD)/blackbit-test-bios.img: $(BUILD)/stage1.bin $(BUILD)/stage2-test.bin $(BUILD)/kernel-test.bin bootloader/make_image.py
	python3 bootloader/make_image.py $(BUILD)/stage1.bin $(BUILD)/stage2-test.bin $(BUILD)/kernel-test.bin $@

boot-image: $(BUILD)/blackbit-bios.img
run-bios: run

test: $(BUILD)/blackbit-test-bios.img
	@set +e; timeout 10s qemu-system-i386 -drive file=$(BUILD)/blackbit-test-bios.img,format=raw,if=ide,index=0 -boot c -m 128M -display none -serial stdio -monitor none -no-reboot -device isa-debug-exit,iobase=0xf4,iosize=0x04 >$(BUILD)/selftest.log 2>&1; rc=$$?; set -e; cat $(BUILD)/selftest.log; test $$rc -eq 33; grep -q "SELFTEST: .* passed, 0 failed" $(BUILD)/selftest.log
