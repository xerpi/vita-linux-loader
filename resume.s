	.set BOOTSTRAP_PADDR, 0x1F000000

	.align 4
	.text
	.cpu cortex-a9

	.global resume_function
	.type resume_function, %function
resume_function:
	dsb

	@ Set CONTEXTIDR (Context ID Register) to zero.
	mov r3, #0
	mcr p15, 0, r3, c13, c0, 1
	isb

	ldr r0, =sync_point_1
	bl cpus_sync

	@ Get CPU ID
	mrc p15, 0, r0, c0, c0, 5
	and r0, #0xF
	cmp r0, #0
	bne cpu1_3_cont

	@ CPU0: Identity map the scratchpad using a 1MiB section
	ldr r2, =lvl1_pt_va
	ldr r2, [r2]
	add r2, #(BOOTSTRAP_PADDR >> 20) << 2
	ldr r3, =((BOOTSTRAP_PADDR >> 20) << 20) | 0x91402
	str r3, [r2]
	mcr p15, 0, r2, c7, c14, 1 @ DCCIMVAC (Data Cache line Clean and Invalidate by VA to PoC)
	dsb
	mcr p15, 0, r0, c8, c7, 0 @ TLBIALL (Unified TLB Invalidate All)
	dsb
	isb

	@ Copy the Linux bootstrap payload to the scratchpad
	ldr r0, =BOOTSTRAP_PADDR
	ldr r1, =_binary_linux_bootstrap_bin_start
	ldr r2, =_binary_linux_bootstrap_bin_size
	bl resume_memcpy

	ldr r0, =BOOTSTRAP_PADDR
	ldr r1, =_binary_linux_bootstrap_bin_size
	bl dcache_clean_range

cpu1_3_cont:
	ldr r0, =sync_point_2
	bl cpus_sync

	@ TLBIALL (Unified TLB Invalidate All)
	mcr p15, 0, r0, c8, c7, 0
	dsb
	isb

	@ ICIALLU (Icache Invalidate All to PoU)
	mov r0, #0
	mcr p15, 0, r0, c7, c5, 0
	dsb

	@ Get Linux parameters
	ldr r0, =linux_paddr
	ldr r0, [r0]
	ldr r1, =dtb_paddr
	ldr r1, [r1]

	@ Jump to the payload!
	ldr lr, =BOOTSTRAP_PADDR
	bx lr

@ r0 = sync point address
@ Uses: r0, r1, r2
cpus_sync:
	mrc p15, 0, r1, c0, c0, 5
	and r1, #0xF
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

@ r0 = addr, r1 = size
@ Uses: r0, r1
dcache_clean_range:
	add r1, r0
	bic r0, #(32 - 1)
	dsb
1:
	mcr p15, 0, r0, c7, c10, 1 @ DCCMVAC (Data Cache line Clean by VA to PoC)
	add r0, #32
	cmp r0, r1
	blo 1b
	dsb
	bx lr

@ r0 = dst, r1 = src, r2 = size
@ Uses: r0, r1, r2, r3
resume_memcpy:
	ldmia r1!, {r3}
	stmia r0!, {r3}
	subs r2, #4
	bne resume_memcpy
	bx lr

	.data
sync_point_1: .word 0
sync_point_2: .word 0
