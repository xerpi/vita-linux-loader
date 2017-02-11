#include <psp2kern/kernel/cpu.h>
#include <psp2kern/kernel/modulemgr.h>
#include <psp2kern/kernel/threadmgr.h>
#include <psp2kern/kernel/sysmem.h>
#include <psp2kern/io/fcntl.h>
#include <psp2kern/display.h>
#include "log.h"

void _start() __attribute__ ((weak, alias ("module_start")));

#define ALIGN(x, a) (((x) + ((a) - 1)) & ~((a) - 1))

#define LINUX_FILENAME "ux0:/linux/zImage"
#define DTB_FILENAME "ux0:/linux/vita.dtb"

#define SCREEN_PITCH 1024
#define SCREEN_W 960
#define SCREEN_H 544

extern int ksceKernelGetPaddr(unsigned long vaddr, unsigned long *paddr);

extern const unsigned char _binary_payload_bin_start;
extern const unsigned char _binary_payload_bin_size;

static const unsigned int payload_size = (unsigned int)&_binary_payload_bin_size;
static const void *const payload_addr = (void *)&_binary_payload_bin_start;

static unsigned long get_cpu_id(void);
static unsigned long get_vector_base_address(void);
static unsigned long get_ttbr0(void);
static unsigned long get_ttbcr(void);
static unsigned long get_paddr(unsigned long vaddr);
static int find_paddr(unsigned long paddr, unsigned long vaddr, unsigned int size,
		      unsigned int step, unsigned long *found_vaddr);
static unsigned long page_table_entry(unsigned long paddr);
static void map_scratchpad(void);
static int load_file(const char *path, SceUID *uid, void **addr, unsigned int *size);
static int map_framebuffer(void);
static void unmap_framebuffer(void);

static SceUID framebuffer_uid = -1;

struct payload_args {
	unsigned long kernel_paddr;
	unsigned long dtb_paddr;
} __attribute__((packed));

static int payload_trampoline_thread(SceSize args, void *argp)
{
	/*
	 * Disable IRQs and FIQs
	 */
	asm volatile("cpsid if\n");

	/*
	 * Map the scratchpad to VA 0x00000000-0x00007FFF.
	 */
	map_scratchpad();

	/*
	 * Jump to the payload
	 */
	asm volatile(
		"mov r0, #0\n"
		"mov r1, #0xFFFFFFFF\n"
		/* Data sync barrier */
		"dsb\n"
		/* DACR unrestricted */
		"mcr p15, 0, r1, c3, c0, 0\n"
		/* TLB invalidate */
		"mcr p15, 0, r0, c8, c6, 0\n"
		"mcr p15, 0, r0, c8, c5, 0\n"
		"mcr p15, 0, r0, c8, c7, 0\n"
		"mcr p15, 0, r0, c8, c3, 0\n"
		/* Branch predictor invalidate all */
		"mcr p15, 0, r0, c7, c5, 6\n"
		/* Branch predictor invalidate all (IS) */
		"mcr p15, 0, r0, c7, c1, 6\n"
		/* Instruction cache invalidate all (PoU) */
		"mcr p15, 0, r0, c7, c5, 0\n"
		/* Instruction cache invalidate all (PoU, IS) */
		"mcr p15, 0, r0, c7, c1, 0\n"
		/* Instruction barrier */
		"mcr p15, 0, r0, c7, c5, 4\n"
		"mov lr, %0\n"
		"bx lr\n"
		: : "r"(0x00000000 + sizeof(struct payload_args)) : "r0", "r1");
	return 0;
}


int module_start(SceSize argc, const void *args)
{
	int ret;
	SceUID linux_uid;
	SceUID dtb_uid;
	void *linux_vaddr;
	void *dtb_vaddr;
	unsigned int linux_size;
	unsigned int dtb_size;
	unsigned long linux_paddr;
	unsigned long dtb_paddr;

	log_reset();
	LOG("Linux loader by xerpi\n");
	LOG("Payload size: 0x%08X\n", payload_size);

	/*
	 * Map the scratchpad to VA 0x00000000-0x00007FFF.
	 */
	map_scratchpad();
	LOG("Scratchpad mapped\n");

	/*
	 * Copy the payload to the scratchpad.
	 */
	ksceKernelCpuUnrestrictedMemcpy(0x00000000, payload_addr, payload_size);
	ksceKernelCpuDcacheWritebackRange((void *)payload_addr, payload_size);
	ksceKernelCpuIcacheAndL2WritebackInvalidateRange((void *)payload_addr, payload_size);
	LOG("Payload copied to the scratchpad\n");

	LOG("\n");

	/*
	 * Load the Linux files (kernel image and device tree blob).
	 */
	ret = load_file(LINUX_FILENAME, &linux_uid, &linux_vaddr, &linux_size);
	if (ret < 0) {
		LOG("Error loading " LINUX_FILENAME ": 0x%08X\n", ret);
		goto error_load_linux_image;
	}

	linux_paddr = get_paddr((unsigned int)linux_vaddr);

	LOG("Linux memory UID: 0x%08X\n", linux_uid);
	LOG("Linux load vaddr: 0x%08X\n", (unsigned int)linux_vaddr);
	LOG("Linux load paddr: 0x%08lX\n", linux_paddr);
	LOG("Linux size: 0x%08X\n", linux_size);

	LOG("\n");

	ret = load_file(DTB_FILENAME, &dtb_uid, &dtb_vaddr, &dtb_size);
	if (ret < 0) {
		LOG("Error loading " DTB_FILENAME ": 0x%08X\n", ret);
		goto error_load_dtb;
	}

	dtb_paddr = get_paddr((unsigned int)dtb_vaddr);

	LOG("DTB memory UID: 0x%08X\n", dtb_uid);
	LOG("DTB load vaddr: 0x%08X\n", (unsigned int)dtb_vaddr);
	LOG("DTB load paddr: 0x%08lX\n", dtb_paddr);
	LOG("DTB size: 0x%08X\n", dtb_size);

	LOG("\n");

	/*
	 * Setup the payload arguments.
	 */
	struct payload_args payload_args;
	payload_args.kernel_paddr = linux_paddr;
	payload_args.dtb_paddr = dtb_paddr;

	ksceKernelCpuUnrestrictedMemcpy(0x00000000, &payload_args, sizeof(payload_args));
	ksceKernelCpuDcacheAndL2WritebackRange((void *)payload_addr, payload_size);

	/*
	 * Map the framebuffer.
	 */
	ret = map_framebuffer();
	if (ret < 0) {
		LOG("Error mapping the framebuffer: 0x%08X\n", ret);
		goto error_map_framebuffer;
	}
	LOG("Framebuffer mapped\n");

	/*
	 * Hook the IRQ vector handler and make it jump to the payload
	 */
	/*unsigned long irq_vector_addr = get_vector_base_address() + 0x18;

	#define ARM_BRANCH(cur, dst) ((0b1110 << 28) | (0b101 << 25) | (0b0 << 24) | (((dst) - ((cur) + 8)) >> 2))

	unsigned long irq_vector_value = ARM_BRANCH(irq_vector_addr, (unsigned long)&payload_trampoline_thread);

	ksceKernelCpuUnrestrictedMemcpy((void *)irq_vector_addr, &irq_vector_value, sizeof(irq_vector_value));
	ksceKernelCpuDcacheAndL2WritebackRange((void *)irq_vector_addr, sizeof(irq_vector_value));

	while (1)
		ksceKernelDelayThread(1000);*/

	int i;
	for (i = 0; i < 4; i++) {
		SceUID thid = ksceKernelCreateThread("trampoline",
			payload_trampoline_thread, 0x3C, 0x1000, 0, 1 << i, 0);

		ksceKernelStartThread(thid, 0, NULL);
	}

	while (1)
		ksceKernelDelayThread(1000);

	unmap_framebuffer();

	ksceKernelFreeMemBlock(linux_uid);
	ksceKernelFreeMemBlock(dtb_uid);

	return SCE_KERNEL_START_SUCCESS;

error_map_framebuffer:
	ksceKernelFreeMemBlock(dtb_uid);
error_load_dtb:
	ksceKernelFreeMemBlock(linux_uid);
error_load_linux_image:
	return SCE_KERNEL_START_FAILED;
}

int module_stop(SceSize argc, const void *args)
{
	return SCE_KERNEL_STOP_SUCCESS;
}

unsigned long get_cpu_id(void)
{
	unsigned long mpidr;

	asm volatile("mrc p15, 0, %0, c0, c0, 5\n" : "=r"(mpidr));

	return mpidr & 3;
}

unsigned long get_vector_base_address(void)
{
	unsigned long address;

	asm volatile("mrc p15, 0, %0, c12, c0, 0\n" : "=r"(address));

	return address;
}

unsigned long get_ttbr0(void)
{
	unsigned long ttbr0;

	asm volatile("mrc p15, 0, %0, c2, c0, 0\n" : "=r"(ttbr0));

	return ttbr0;
}

unsigned long get_ttbcr(void)
{
	unsigned long ttbcr;

	asm volatile("mrc p15, 0, %0, c2, c0, 2\n" : "=r"(ttbcr));

	return ttbcr;
}
unsigned long get_paddr(unsigned long vaddr)
{
	unsigned long paddr;

	ksceKernelGetPaddr(vaddr, &paddr);

	return paddr;
}

int find_paddr(unsigned long paddr, unsigned long vaddr, unsigned int size, unsigned int step, unsigned long *found_vaddr)
{
	unsigned long vaddr_end = vaddr + size;

	for (; vaddr < vaddr_end; vaddr += step) {
		unsigned long cur_paddr = get_paddr(vaddr);

		if ((cur_paddr & ~(step - 1)) == (paddr & ~(step - 1))) {
			if (found_vaddr)
				*found_vaddr = vaddr;
			return 1;
		}
	}

	return 0;
}

unsigned long page_table_entry(unsigned long paddr)
{
	unsigned long base_addr = paddr >> 12;
	unsigned long XN        = 0b0;   /* XN disabled */
	unsigned long C_B       = 0b11;  /* Inner cacheable memory: Write-Back, no Write-Allocate */
	unsigned long AP_1_0    = 0b11;  /* Full access */
	unsigned long TEX_2_0   = 0b111; /* Outer cacheable memory: Write-Back, no Write-Allocate */
	unsigned long AP_2      = 0b0;   /* Full access */
	unsigned long S         = 0b1;   /* Shareable */
	unsigned long nG        = 0b0;   /* Global translation */

	return  (base_addr << 12) |
		(nG        << 11) |
		(S         << 10) |
		(AP_2      <<  9) |
		(TEX_2_0   <<  6) |
		(AP_1_0    <<  4) |
		(C_B       <<  2) |
		(1         <<  1) |
		(XN        <<  0);
}

void map_scratchpad(void)
{
	int i;
	unsigned long ttbcr_n;
	unsigned long ttbr0_paddr;
	unsigned long ttbr0_vaddr;
	unsigned long first_page_table_paddr;
	unsigned long first_page_table_vaddr;
	unsigned long pt_entries[4];

	/*
	 * Identity-map the scratchpad (PA 0x00000000-0x00007FFF) to
	 * the VA 0x00000000-0x00007FFF.
	 * To do such thing we will use the first 4 PTEs of the
	 * first page table of TTBR0 (which aren't used).
	 */

	ttbcr_n = get_ttbcr() & 7;
	ttbr0_paddr = get_ttbr0() & ~((1 << (14 - ttbcr_n)) - 1);
	find_paddr(ttbr0_paddr, 0, 0xFFFFFFFF, 0x1000, &ttbr0_vaddr);

	first_page_table_paddr = (*(unsigned int *)ttbr0_vaddr) & 0xFFFFFC00;
	find_paddr(first_page_table_paddr, 0, 0xFFFFFFFF, 0x1000, &first_page_table_vaddr);

	/*LOG("ttbr0_paddr: 0x%08lX\n", ttbr0_paddr);
	LOG("ttbr0_vaddr: 0x%08lX\n", ttbr0_vaddr);
	LOG("First page table paddr: 0x%08lX\n", first_page_table_paddr);
	LOG("First page table vaddr: 0x%08lX\n", first_page_table_vaddr);
	LOG("\n");*/

	for (i = 0; i < 4; i++) {
		pt_entries[i] = page_table_entry(i << 12);
	}

	ksceKernelCpuUnrestrictedMemcpy((void *)first_page_table_vaddr, pt_entries, sizeof(pt_entries));
	ksceKernelCpuDcacheAndL2WritebackRange((void *)first_page_table_vaddr, sizeof(pt_entries));
}

int alloc_phycont(unsigned int size, SceUID *uid, void **addr)
{
	int ret;
	SceUID mem_uid;
	unsigned int mem_size;
	void *mem_addr;

	mem_size = ALIGN(size, 4096);

	mem_uid = ksceKernelAllocMemBlock("phycont", 0x30808006, mem_size, NULL);
	if (mem_uid < 0)
		return mem_uid;

	ret = ksceKernelGetMemBlockBase(mem_uid, &mem_addr);
	if (ret < 0) {
		ksceKernelFreeMemBlock(mem_uid);
		return ret;
	}

	if (uid)
		*uid = mem_uid;
	if (addr)
		*addr = mem_addr;

	return 0;
}

int load_file(const char *path, SceUID *uid, void **addr, unsigned int *size)
{
	int ret;
	SceUID fd;
	SceUID mem_uid;
	void *mem_addr;
	unsigned int file_size;

	fd = ksceIoOpen(path, SCE_O_RDONLY, 0);
	if (fd < 0)
		return fd;

	file_size = ksceIoLseek(fd, 0, SCE_SEEK_END);
	ksceIoLseek(fd, 0, SCE_SEEK_SET);

	ret = alloc_phycont(file_size, &mem_uid, &mem_addr);
	if (ret < 0) {
		ksceIoClose(fd);
		return ret;
	}

	ksceIoRead(fd, mem_addr, file_size);

	ksceIoClose(fd);

	if (uid)
		*uid = mem_uid;
	if (addr)
		*addr = mem_addr;
	if (size)
		*size = file_size;

	return 0;
}

int map_framebuffer(void)
{
	const unsigned int fb_size = ALIGN(4 * SCREEN_PITCH * SCREEN_H, 256 * 1024);
	int ret;
	SceDisplayFrameBuf fb;
	void *fb_addr;

	framebuffer_uid = ksceKernelAllocMemBlock("fb", 0x40404006 , fb_size, NULL);
	if (framebuffer_uid < 0)
		return framebuffer_uid;

	ret = ksceKernelGetMemBlockBase(framebuffer_uid, &fb_addr);
	if (ret < 0)
		return ret;

	memset(fb_addr, 0xFF, 4 * SCREEN_PITCH * SCREEN_H);

	LOG("Framebuffer uid: 0x%08X\n", framebuffer_uid);
	LOG("Framebuffer vaddr: 0x%08X\n", (uintptr_t)fb_addr);
	LOG("Framebuffer paddr: 0x%08lX\n", get_paddr((uintptr_t)fb_addr));
	LOG("Framebuffer size: 0x%08X\n", fb_size);

	memset(&fb, 0, sizeof(fb));
	fb.size        = sizeof(fb);
	fb.base        = fb_addr;
	fb.pitch       = SCREEN_PITCH;
	fb.pixelformat = SCE_DISPLAY_PIXELFORMAT_A8B8G8R8;
	fb.width       = SCREEN_W;
	fb.height      = SCREEN_H;

	return ksceDisplaySetFrameBuf(&fb, SCE_DISPLAY_SETBUF_NEXTFRAME);
}

void unmap_framebuffer(void)
{
	if (framebuffer_uid >= 0)
		ksceKernelFreeMemBlock(framebuffer_uid);
}
