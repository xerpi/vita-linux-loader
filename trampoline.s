	.cpu cortex-a9
	.align 4
	.code 32

	.text

	.global trampoline_stage_0
	.type trampoline_stage_0, %function
trampoline_stage_0:
	dsb
	mov r3, #0
	mcr p15, 0, r3, c13, c0, 1
	isb
	mcr p15, 0, r0, c2, c0, 1
	isb
	dsb
	mcr p15, 0, r1, c13, c0, 1
	mcr p15, 0, r2, c3, c0, 1
	dsb
	isb
	mov r0, #0
	mcr p15, 0, r0, c8, c7, 0

	mrc p15, 0, r0, c0, c0, 5
	ands r0, r0, #0xF

	ldr r1, =sync

	cmp r0, #0
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

	mov r0, r0, lsl #10
	ldr r1, =scratchpad_vaddr
	ldr r1, [r1]
	adds r1, #0x7000
	adds r0, r0, r1
	mov r1, #0x400
	mov r2, #0
	mov r3, #0

clear_stack:
	strd r2, r3, [r0], #8
	subs r1, #8
	bne clear_stack

	cpsid aif, #0x1F
	mov sp, r0
	blx trampoline_stage_1

	nop
	bkpt

	.data
	.balign 4
sync: .word 0

	.ltorg
