	.set BOOTSTRAP_PADDR, 0x1F000000

	.align 4
	.text
	.cpu cortex-a9


	.global resume_function
	.type resume_function, %function
resume_function:
	dsb

	@ Get CPU ID
	mrc p15, 0, r0, c0, c0, 5
	ands r0, r0, #0xF

	ldr r1, =sync
	cmp r0, #0
	bne sync_loop_1

	@ CPU0: Identity map the scratchpad using a 1MiB section
	ldr r2, =lvl1_pt_va
	ldr r2, [r2]
	add r2, #(BOOTSTRAP_PADDR >> 20) << 2
	ldr r3, =((BOOTSTRAP_PADDR >> 20) << 20) | 0x91402
	str r3, [r2]
	dsb

	@ TLBIALL (Unified TLB Invalidate All)
	mcr p15, 0, r0, c8, c7, 0
	isb

	mov r4, r0
	mov r5, r1

	@ Copy the Linux bootstrap payload to the scratchpad
	ldr r0, =BOOTSTRAP_PADDR
	ldr r1, =_binary_linux_bootstrap_bin_start
	ldr r2, =_binary_linux_bootstrap_bin_size
	bl resume_memcpy

	mov r0, r4
	mov r1, r5

	streq r0, [r1]

sync_loop_1:
	ldrb r2, [r1]
	cmp r0, r2
	wfene
	bne sync_loop_1

	ldrh r2, [r1]
	adds r2, #1
	adds r2, r2, #0x100
	strh r2, [r1]
	dsb
	sev

sync_loop_2:
	ldrb r2, [r1, #1]
	cmp r2, #4
	wfene
	bne sync_loop_2

	@ Now all the CPUs have reached this resume function. Let's proceed.

	@ TLBIALL (Unified TLB Invalidate All)
	mcr p15, 0, r0, c8, c7, 0
	isb

	@ ICIALLU (Icache Invalidate All to PoU)
	mov r0, #0
	mcr p15, 0, r0, c7, c5, 0
	dsb

	@ Dcache clean and invalidate all
	mov r0, #0
1:
	mov r1, #0
2:
	orr r2, r1, r0
	mcr p15, 0, r2, c7, c14, 2 @ DCCISW - Data cache clean and invalidate by set/way
	add r1, r1, #0x40000000
	cmp r1, #0
	bne 2b
	add r0, r0, #0x20
	cmp r0, #0x2000
	bne 2b
	dsb

	@ Get Linux parameters
	ldr r0, =linux_paddr
	ldr r0, [r0]
	ldr r1, =dtb_paddr
	ldr r1, [r1]

	@ Jump to the payload!
	ldr lr, =BOOTSTRAP_PADDR
	bx lr

@ r0 = dst, r1 = src, r2 = size
resume_memcpy:
	mov r12, r0
1:
	ldmia r1!, {r3}
	stmia r0!, {r3}
	subs r2, #4
	bne 1b
	mov r0, r12
	bx lr

	.ltorg

	.data
sync: .word 0
