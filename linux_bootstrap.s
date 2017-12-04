	.align 4
	.section .text.linux_bootstrap
	.cpu cortex-a9

	.globl _start
@ r0 = Linux entry paddr, r1 = DTB paddr
_start:
	dsb
	mov r9, r0
	mov r10, r1

	@ Now we are in an identity-mapped region, let's disable
	@ the MMU, the Dcache and the Icache
	mrc p15, 0, r0, c1, c0, 0
	bic r0, #1 << 0		@ MMU
	bic r0, #1 << 2		@ Dcache
	bic r0, #1 << 12	@ Icache
	mcr p15, 0, r0, c1, c0, 0

	@ Get CPU ID
	mrc p15, 0, r0, c0, c0, 5
	and r0, #0xF
	cmp r0, #0
	beq cpu0_cont
1:
	wfe
	b 1b

cpu0_cont:

	@ Enable the UART clock (pervasive)
	ldr r0, =0xE3102000	@ ScePervasiveGate
	ldr r1, =0xE3101000	@ ScePervasiveResetReg

	ldr r2, [r0, #0x120]	@ Clock enable
	orr r2, #1
	str r2, [r0, #0x120]

	ldr r2, [r1, #0x120]	@ Reset disable
	bic r2, #1
	str r2, [r1, #0x120]

	@ Setup the UART
	ldr r0, =0xE2030000	@ SceUartReg
	ldr r1, =0xE3105000	@ SceUartClkgenReg

	mov r2, #0		@ Disable the device
	str r2, [r0, #4]
	dsb

	ldr r2, =0x1001A	@ Baudrate = 115200
	str r2, [r1]

	mov r2, #1		@ UART config
	str r2, [r0, #0x10]
	mov r2, #3
	str r2, [r0, #0x20]
	mov r2, #0
	str r2, [r0, #0x30]
	str r2, [r0, #0x40]
	str r2, [r0, #0x50]
	mov r2, #0x303
	str r2, [r0, #0x60]
	ldr r2, =0x10001
	str r2, [r0, #0x64]
	dsb

	mov r2, #1		@ Enable the device
	str r2, [r0, #4]
	dsb

	mov r0, #0
	@ BPIALL (Branch Predictor Invalidate All)
	mcr p15, 0, r0, c7, c5, 6
	isb
	@ ICIALLU (Icache Invalidate All to PoU)
	mcr p15, 0, r0, c7, c5, 0
	dsb
	@ TLBIALL (Unified TLB Invalidate All)
	mcr p15, 0, r0, c8, c7, 0
	isb

	@ Dcache invalidate all
	mov r0, #0
1:
	mov r1, #0
2:
	orr r2, r1, r0
	mcr p15, 0, r2, c7, c6, 2 @ DCISW - Data cache invalidate by set/way
	add r1, r1, #0x40000000
	cmp r1, #0
	bne 2b
	add r0, r0, #0x20
	cmp r0, #0x2000
	bne 2b
	dsb

	@ Jump to Linux!
	mov r0, #0
	mvn r1, #0
	mov r2, r10
	mov lr, r9
	bx lr

	.ltorg

	.data
