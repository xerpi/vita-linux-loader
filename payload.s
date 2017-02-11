	.cpu cortex-a9
	.align 4
	.code 32

	.text

	.global _start
_start:

# This is loaded at address 0, and the exception vectors are configured to be there
_exception_vectors_table:
	ldr pc, _reset_h
	ldr pc, _undefined_instruction_vector_h
	ldr pc, _software_interrupt_vector_h
	ldr pc, _prefetch_abort_vector_h
	ldr pc, _data_abort_vector_h
	ldr pc, _reserved_handler_h
	ldr pc, _interrupt_vector_h
	ldr pc, _fast_interrupt_vector_h

_reset_h:                           .word payload_code
_undefined_instruction_vector_h:    .word _stub_interrupt_vector_handler
_software_interrupt_vector_h:       .word _stub_interrupt_vector_handler
_prefetch_abort_vector_h:           .word _stub_interrupt_vector_handler
_data_abort_vector_h:               .word _stub_interrupt_vector_handler
_reserved_handler_h:                .word _stub_interrupt_vector_handler
_interrupt_vector_h:                .word payload_code
_fast_interrupt_vector_h:           .word _stub_interrupt_vector_handler

# Payload args
arg_kernel_paddr: .word 0
arg_dtb_paddr: .word 0

_stub_interrupt_vector_handler:
	subs pc, lr, #4

payload_code:
	# Disable interrupts
	cpsid if

	# DACR unrestricted
	ldr r0, =0xFFFFFFFF
	mcr p15, 0, r0, c3, c0, 0

	# Read CPU ID
	mrc p15, 0, r0, c0, c0, 5
	and r0, r0, #3

	# Notify that we are here
	mov r1, #1
	ldr r2, =cpu_sync
	str r1, [r2, r0, lsl #2]

	# If we are the CPU[1, 3], then WFI
	cmp r0, #0
	beq cpu0_continue

wfi_loop:
	wfi
	b wfi_loop

cpu0_continue:
	# Wait until all the CPUs have reached the payload_code
	ldr r0, =cpu_sync
1:
	ldr r1, [r0, #0x0]
	ldr r2, [r0, #0x4]
	ldr r3, [r0, #0x8]
	ldr r4, [r0, #0xC]
	add r1, r1, r2
	add r1, r1, r3
	add r1, r1, r4
	cmp r1, #4
	blt 1b

	# Now let's to disable the MMU and the data cache.
	# Since we are in an identity-mapped region, it should be ok.
	mrc p15, 0, r0, c1, c0, 0
	bic r0, #0b101
	mcr p15, 0, r0, c1, c0, 0

	# Disable private timer and watchdog
	ldr r0, =0x1A002000
	mov r1, #0
	str r1, [r0, #(0x0600 + 0x08)]
	str r1, [r0, #(0x0600 + 0x28)]

	# Unlock all L2 cache lines
	ldr r0, =0x1A002000
	ldr r1, =0xFFFF
	str r1, [r0, #0x954]
	dmb

	# Clean and invalidate L2 cache
	str r1, [r0, #0x7FC]
	dmb

	# L2 cache sync
	mov r1, #0
	str r1, [r0, #0x730]
1:
	ldr r1, [r0, #0x730]
	tst r1, #1
	bne 1b
	dmb

	# Disable L2 cache
	mov r1, #0
	str r1, [r0, #0x100]
	dmb

	# Setup the SP at the end of the scratchpad
	ldr sp, =0x00008000

	# Setup kernel args
	ldr r0, =0
	ldr r1, =0xFFFFFFFF
	ldr r2, =arg_dtb_paddr
	ldr r2, [r2]

	# Jump to the kernel!
	ldr lr, =arg_kernel_paddr
	ldr lr, [lr]
	bx lr

# Variables
cpu_sync: .word 0, 0, 0, 0

	.ltorg
