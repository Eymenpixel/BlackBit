.code16
.section .text
.global _start
_start:
    cli
    xorw %ax,%ax
    movw %ax,%ds
    movw %ax,%es
    movw %ax,%ss
    movw $0x7c00,%sp
    sti
    movb %dl,0x0500
    movw $stage2_dap,%si
    movb $0x42,%ah
    movb 0x0500,%dl
    int $0x13
    jc boot_error
    ljmp $0x0000,$0x8000
boot_error:
    movw $0x0e45,%ax
    movw $0x0007,%bx
    int $0x10
1:  hlt
    jmp 1b

.align 4
stage2_dap:
    .byte 0x10,0
    .word 4
    .word 0
    .word 0x0800
    .long 1
    .long 0
    .org 510,0
    .word 0xaa55
