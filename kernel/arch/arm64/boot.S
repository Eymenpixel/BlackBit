.section .text.boot,"ax"
.global _start
.type _start, %function
_start:
    b 1f
    .long 0
    .quad 0x80000
    .quad _image_end - _start
    .quad 0
    .quad 0
    .quad 0
    .long 0x644d5241
    .long 0
1:
    msr daifset, #0xf
    ldr x9, =0x09000000
    adr x10, banner
2:
    ldrb w11, [x10], #1
    cbz w11, 3f
    str w11, [x9]
    b 2b
3:
    wfe
    b 3b

.align 4
banner:
    .asciz "Blackbit ARM64 bootstrap kernel. UART ready; hardware and userspace bring-up pending.\r\n"
