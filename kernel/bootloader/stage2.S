#ifndef KERNEL_BYTES
#error KERNEL_BYTES must be defined
#endif
#ifndef KERNEL_SECTORS
#error KERNEL_SECTORS must be defined
#endif
#if KERNEL_SECTORS < 1 || KERNEL_SECTORS > 127
#error BIOS EDD read size must be between 1 and 127 sectors
#endif
.code16
.section .text
.global stage2_start
stage2_start:
    cli
    xorw %ax,%ax
    movw %ax,%ds
    movw %ax,%es
    movw %ax,%ss
    movw $0x7c00,%sp
    sti

    /* Gather a bounded E820 map for the kernel's existing Multiboot2 parser. */
    movw $0x7018,%di
    xorw %ax,%ax
    movw %ax,e820_count
    xorl %ebx,%ebx
1:
    xorw %ax,%ax
    movw %ax,%es
    movl $1,%es:20(%di)
    movl $0xe820,%eax
    movl $0x534d4150,%edx
    movl $24,%ecx
    int $0x15
    jc e820_done
    cmpl $0x534d4150,%eax
    jne e820_done
    incw e820_count
    addw $24,%di
    cmpw $16,e820_count
    jae e820_done
    testl %ebx,%ebx
    jnz 1b

e820_done:
    cmpw $0,e820_count
    je boot_error
    movl $0,0x7004
    movl $6,0x7008
    movzwl e820_count,%eax
    imull $24,%eax
    movl %eax,%edx
    addl $16,%eax
    movl %eax,0x700c
    movl $24,0x7010
    movl $0,0x7014
    addl $32,%edx
    movl %edx,0x7000
    movw e820_count,%ax
    imulw $24,%ax
    addw $0x7018,%ax
    movw %ax,%di
    movl $0,(%di)
    movl $8,4(%di)

    /* Load the contiguous flat kernel payload from fixed LBA 5. */
    movw $kernel_dap,%si
    movw $KERNEL_SECTORS,kernel_dap+2
    movw $0,kernel_dap+4
    movw $0x1000,kernel_dap+6
    movl $5,kernel_dap+8
    movl $0,kernel_dap+12
    movb 0x0500,%dl
    movb $0x42,%ah
    int $0x13
    jc boot_error

    /* Enable A20 before entering flat 32-bit protected mode. */
    inb $0x92,%al
    orb $0x02,%al
    andb $0xfe,%al
    outb %al,$0x92
    lgdt gdt_descriptor
    movl %cr0,%eax
    orl $1,%eax
    movl %eax,%cr0
    ljmp $0x08,$protected_entry

boot_error:
    movw $0x0e45,%ax
    movw $0x0007,%bx
    int $0x10
2:  cli
    hlt
    jmp 2b

.align 4
kernel_dap:
    .byte 0x10,0
    .word KERNEL_SECTORS
    .word 0
    .word 0x1000
    .long 5
    .long 0
e820_count:
    .word 0

.align 8
gdt:
    .quad 0
    .quad 0x00cf9a000000ffff
    .quad 0x00cf92000000ffff
gdt_end:
gdt_descriptor:
    .word gdt_end-gdt-1
    .long gdt

.code32
protected_entry:
    movw $0x10,%ax
    movw %ax,%ds
    movw %ax,%es
    movw %ax,%ss
    movw %ax,%fs
    movw %ax,%gs
    movl $0x90000,%esp
    cld
    movl $0x10000,%esi
    movl $0x100000,%edi
    movl $KERNEL_BYTES,%ecx
    rep movsb
    movl $0x36d76289,%eax
    movl $0x7000,%ebx
    movl $0x101000,%edx
    jmp *%edx
