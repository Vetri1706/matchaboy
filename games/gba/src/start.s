/* SPDX-License-Identifier: GPL-3.0-only */
.syntax unified
.cpu arm7tdmi
.arm
.section .header,"ax"
.global _start
_start:
    b boot
    .space 188
.section .text.boot,"ax"
boot:
    mov r0,#0x1f
    msr cpsr_c,r0
    ldr sp,=0x03007e00
    ldr r0,=__bss_start
    ldr r1,=__bss_end
    mov r2,#0
1:  cmp r0,r1
    strlo r2,[r0],#4
    blo 1b
    ldr r0,=__data_load
    ldr r1,=__data_start
    ldr r2,=__data_end
2:  cmp r1,r2
    ldrlo r3,[r0],#4
    strlo r3,[r1],#4
    blo 2b
    ldr r0,=__code_load
    ldr r1,=__code_start
    ldr r2,=__code_end
4:  cmp r1,r2
    ldrlo r3,[r0],#4
    strlo r3,[r1],#4
    blo 4b
    ldr r3,=main
    bx r3
3:  b 3b
