.code32
.section .multiboot,"a"
.align 8
mb2_header:
    .long 0xe85250d6
    .long 0
    .long mb2_header_end - mb2_header
    .long -(0xe85250d6 + (mb2_header_end - mb2_header))
    .short 0
    .short 0
    .long 8
mb2_header_end:

.section .bss
.align 4096
pml4: .skip 4096
pdpt: .skip 4096
pd:   .skip 4096
.align 16
stack_bottom: .skip 32768
stack_top:

.section .text
.global _start
.type _start, @function
_start:
    cli
    mov %eax, boot_magic
    mov %ebx, boot_info
    mov $stack_top, %esp

    /* Identity map the first GiB with 2 MiB pages for the long-mode jump. */
    mov $pdpt, %eax
    or $3, %eax
    mov %eax, pml4
    mov $pd, %eax
    or $3, %eax
    mov %eax, pdpt
    mov $pd, %edi
    mov $0x83, %eax
    mov $512, %ecx
1:
    mov %eax, (%edi)
    movl $0, 4(%edi)
    add $8, %edi
    add $0x200000, %eax
    loop 1b

    mov $pml4, %eax
    mov %eax, %cr3
    mov %cr4, %eax
    or $0x20, %eax
    mov %eax, %cr4
    mov $0xc0000080, %ecx
    rdmsr
    or $0x100, %eax
    wrmsr
    lgdt gdt64_descriptor
    mov %cr0, %eax
    or $0x80000000, %eax
    mov %eax, %cr0
    ljmp $0x08, $long_mode_entry

.align 8
gdt64:
    .quad 0
    .quad 0x00af9a000000ffff
    .quad 0x00cf92000000ffff
gdt64_end:
gdt64_descriptor:
    .word gdt64_end-gdt64-1
    .long gdt64

.code64
long_mode_entry:
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss
    mov $stack_top, %rsp
    and $-16, %rsp
    mov boot_magic(%rip), %edi
    mov boot_info(%rip), %esi
    call kernel_main
2:  hlt
    jmp 2b

.global inb
inb:
    mov %di, %dx
    xor %eax, %eax
    in %dx, %al
    ret

.global outb
outb:
    mov %di, %dx
    mov %sil, %al
    out %al, %dx
    ret

.global cpu_halt
cpu_halt:
    hlt
    ret

.section .data
.align 4
boot_magic: .long 0
boot_info:  .long 0

.section .note.GNU-stack,"",@progbits
