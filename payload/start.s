	.cpu cortex-a9
	.align 4
	.code 32

	.text

	.global _start
_start:

# Payload args
arg_kernel_paddr: .word 0
arg_dtb_paddr: .word 0
arg_pl310_vaddr: .word 0

payload_code:
	# Disable interrupts and enter System mode
	cpsid aif, #0x1F

	clrex
	dsb
	isb

	# DACR unrestricted
	mov r0, #0xFFFFFFFF
	mcr p15, 0, r0, c3, c0, 0

	# Flush Dcache
	mov	r2, #0
flush:
	mcr	p15, 0, r2, c7, c10, 2
	adds	r2, r2, #0x40000000
	bcc	flush
	adds	r2, #0x20
	lsrs	r0, r2, #0xD
	beq     flush
	dsb

	# MMU off, Dcache & Icache disabled
	#ldr r0, =0x00C50078
	mrc p15, 0, r0, c1, c0, 0
	bic r0, #(1 << 12)
	bic r0, #(1 << 2)
	bic r0, #(1 << 0)
	mcr p15, 0, r0, c1, c0, 0
	isb

	# Read CPU ID
	mrc p15, 0, r0, c0, c0, 5
	ands r0, r0, #0xF

	ldr r1, =cpu_sync

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

	# Read CPU ID
	mrc p15, 0, r0, c0, c0, 5
	and r0, r0, #0xF

	cmp r0, #0
	beq cpu0_cont

1:
	wfe
	b 1b

cpu0_cont:

	# Clean and invalidate all + disable L2
	ldr r12, =0x16A
	mov r0, #0
	smc #0
	isb
	dsb

	# Disable SCU (def value: 0x00000061);
	# ldr r0, =0x1A000000
	# ldr r0, [r0, #0x00]
	# bic r0, #1
	# str r0, [r0, #0x00]
	# dsb

	ldr r0, =0x48000000
	ldr r1, arg_dtb_paddr
	ldr r2, =0x10000
	bl memcpy

	ldr r0, =0x49000000
	ldr r1, arg_kernel_paddr
	ldr r2, =0x1000000
	bl memcpy

	# Setup kernel args
	ldr r0, =0
	ldr r1, =0xFFFFFFFF
	ldr r2, =0x48000000
	ldr lr, =0x49000000
	bx lr

	.ltorg

# r0 = dst
# r1 = src
# r2 = size
memcpy:
	ldr r3, [r1], #4
	str r3, [r0], #4
	sub r2, #4
	cmp r2, #0
	bgt memcpy
	bx lr

	.data
	.balign 4
cpu_sync: .word 0
