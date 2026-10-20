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
.align 16
stack_bottom:
    .skip 16384
stack_top:

.section .text
.global _start
.type _start, @function
_start:
    cli
    mov $stack_top, %esp
    push %ebx
    push %eax
    call kernel_main
1:  hlt
    jmp 1b

.global inb
inb:
    mov 4(%esp), %edx
    xor %eax, %eax
    inb %dx, %al
    ret

.global outb
outb:
    mov 4(%esp), %edx
    mov 8(%esp), %eax
    outb %al, %dx
    ret

.global cpu_halt
cpu_halt:
    hlt
    ret

.section .note.GNU-stack,"",@progbits
