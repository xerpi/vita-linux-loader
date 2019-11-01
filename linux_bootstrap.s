	.set CPU123_WAIT_BASE, 0x1F007F00

	.align 4
	.text
	.cpu cortex-a9

.macro	get_cpu_id, rd
	mrc p15, 0, \rd, c0, c0, 5
	and \rd, #0xF
.endm

	.globl _start
@ r0 = Linux entry paddr, r1 = DTB paddr
_start:
	dsb
	mov r9, r0
	mov r10, r1

	@ Clean jump target address (base + CPU_ID * 4)
	ldr r0, =CPU123_WAIT_BASE
	get_cpu_id r1
	mov r2, #0
	str r2, [r0, r1, lsl #2]

	@ Barrier
	ldr r0, =sync_point_1
	bl cpus_sync

	@ Clean and invalidate the entire Dcache
	bl dcache_clean_inv_all

	@ Identity-mapped region, disable MMU, D/Icache
	mrc p15, 0, r0, c1, c0, 0
	bic r0, #1 << 0		@ MMU
	bic r0, #1 << 2		@ Dcache
	bic r0, #1 << 12	@ Icache
	mcr p15, 0, r0, c1, c0, 0

	@ Invalidate the entire Dcache
	bl dcache_inv_all

	mov r0, #0
	mcr p15, 0, r0, c7, c5, 6 @ BPIALL (Branch Predictor Invalidate All)
	isb
	mcr p15, 0, r0, c7, c5, 0 @ ICIALLU (Icache Invalidate All to PoU)
	dsb
	mcr p15, 0, r0, c8, c7, 0 @ TLBIALL (Unified TLB Invalidate All)
	isb

	@ Check CPU ID
	get_cpu_id r0
	cmp r0, #0
	beq cpu0_cont

	@ CPUs 1,2,3 wait for an address to jump to
	ldr r1, =CPU123_WAIT_BASE
cpu123_wait:
	wfe
	ldr r2, [r1, r0, lsl #2]
	cmp r2, #0
	beq cpu123_wait
	bx r2

cpu0_cont:
	@ Enable the UART
	bl uart_enable

	@ L2 cache clean and invalidate all and disable
	@ XXX: Better to not touch the L2 for now
	@ldr r12, =0x16A
	@mov r0, #0
	@smc #0
	@isb
	@dsb

	@ Jump to Linux!
	mov r0, #0
	mvn r1, #0
	mov r2, r10
	mov lr, r9
	bx lr

@ Uses r0, r1 and r2
uart_enable:
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

	bx lr

@ Uses: r0, r1
dcache_clean_inv_all:
	mov r0, #0
1:
	mcr p15, 0, r0, c7, c14, 2 @ DCCISW (Data cache clean and invalidate by set/way)
	adds r0, r0, #0x40000000
	bcc 1b
	adds r0, #0x20
	lsrs r1, r0, #0xD
	beq 1b
	dsb
	bx lr

@ Uses: r0, r1
dcache_inv_all:
	mov r0, #0
1:
	mcr p15, 0, r0, c7, c6, 2 @ DCISW (Data cache invalidate by set/way)
	adds r0, r0, #0x40000000
	bcc 1b
	adds r0, #0x20
	lsrs r1, r0, #0xD
	beq 1b
	dsb
	bx lr


@ r0 = sync point address
@ Uses: r0, r1, r2
cpus_sync:
	get_cpu_id r1
	cmp r1, #0
	streq r1, [r0]
1:
	ldrb r2, [r0]
	cmp r1, r2
	wfene
	bne 1b
	ldrh r2, [r0]
	adds r2, #1
	adds r2, r2, #0x100
	strh r2, [r0]
	dsb
	sev
1:
	ldrb r2, [r0, #1]
	cmp r2, #4
	wfene
	bne 1b
	bx lr

	.data
sync_point_1: .word 0
