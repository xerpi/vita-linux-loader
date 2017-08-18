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

	# DACR unrestricted
	mov r0, #0xFFFFFFFF
	mcr p15, 0, r0, c3, c0, 0

	# Disable MMU and Dcache
	# ldr r0, =0x00C50078
	# mcr p15, 0, r0, c1, c0, 0
	# isb
	# dsb

	# Read CPU ID
	mrc p15, 0, r0, c0, c0, 5
	and r0, r0, #0xF

	cmp r0, #0
	beq cpu0_cont

1:
	wfe
	b 1b

cpu0_cont:

	# Disable Dcache
	mrc p15, 0, r0, c1, c0, 0
	bic r0, #(1 << 2)
	mcr p15, 0, r0, c1, c0, 0
	isb
	dsb

	# Load PL310 registers virtual address
	ldr r0, =arg_pl310_vaddr
	ldr r0, [r0]

	# Clean and invalidate L2 cache
	ldr r1, =0xFFFF
	str r1, [r0, #0x7FC]
	dmb
1:
	ldr r2, [r0, #0x7FC]
	tst r2, r1
	bne 1b

	# L2 cache sync
	mov r1, #0
	str r1, [r0, #0x730]
	dmb

	ldr r0, =0x00C50078
	mcr p15, 0, r0, c1, c0, 0
	isb
	dsb

	# Copy kernel
	ldr r0, =0x4A000000
	ldr r1, =arg_kernel_paddr
	ldr r1, [r1]
	ldr r2, =4*1024*1024
	bl memcpy

	ldr r0, =0x4B000000
	ldr r1, =arg_dtb_paddr
	ldr r1, [r1]
	ldr r2, =4*1024*1024
	bl memcpy

	# Setup kernel args
	ldr r0, =0
	ldr r1, =0xFFFFFFFF
	ldr r2, =0x4B000000
	ldr lr, =0x4A000000

	bx lr


# r0 = dst
# r1 = src
# r2 = size
memcpy:
	ldr r3, [r1]
	str r3, [r0]
	add r0, #4
	add r1, #4
	sub r2, #4
	cmp r2, #0
	bne memcpy
	bx lr
