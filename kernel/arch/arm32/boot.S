.syntax unified
.arm
.section .text.boot,"ax"
.global _start
.type _start, %function
_start:
    cpsid if
    ldr r0, =0x101f1000
    ldr r1, =banner
1:
    ldrb r2, [r1], #1
    cmp r2, #0
    beq 2f
    str r2, [r0]
    b 1b
2:
    wfi
    b 2b

.align 4
banner:
    .asciz "Blackbit ARM32 bootstrap kernel. PL011 ready; hardware and userspace bring-up pending.\r\n"
