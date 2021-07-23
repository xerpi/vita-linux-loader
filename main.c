#include <stdio.h>
#include <string.h>
#include <psp2kern/kernel/cpu.h>
#include <psp2kern/kernel/modulemgr.h>
#include <psp2kern/kernel/threadmgr.h>
#include <psp2kern/kernel/sysmem.h>
#include <psp2kern/lowio/pervasive.h>
#include <psp2kern/io/fcntl.h>
#include <psp2kern/display.h>
#include <psp2kern/power.h>
#include <psp2kern/sblaimgr.h>
#include <psp2kern/syscon.h>
#include <psp2kern/uart.h>
#include <taihen.h>

#define LOG(s, ...) \
	do { \
		char _buffer[256]; \
		snprintf(_buffer, sizeof(_buffer), s, ##__VA_ARGS__); \
		uart0_print(_buffer); \
	} while (0)

void _start() __attribute__ ((weak, alias ("module_start")));

#define ALIGN(x, a)	(((x) + ((a) - 1)) & ~((a) - 1))

#define LINUX_DIR	"ux0:/linux/"

#define ZIMAGE 		LINUX_DIR "zImage"
#define VITA1000_DTB 	LINUX_DIR "vita1000.dtb"
#define VITA2000_DTB 	LINUX_DIR "vita2000.dtb"
#define PSTV_DTB 	LINUX_DIR "pstv.dtb"

typedef struct SceSysconResumeContext {
	unsigned int size;
	unsigned int unk;
	unsigned int buff_vaddr;
	unsigned int resume_func_vaddr;
	unsigned int SCTLR;
	unsigned int ACTLR;
	unsigned int CPACR;
	unsigned int TTBR0;
	unsigned int TTBR1;
	unsigned int TTBCR;
	unsigned int DACR;
	unsigned int PRRR;
	unsigned int NMRR;
	unsigned int VBAR;
	unsigned int CONTEXTIDR;
	unsigned int TPIDRURW;
	unsigned int TPIDRURO;
	unsigned int TPIDRPRW;
	unsigned int unk2[6];
	unsigned long long time;
} SceSysconResumeContext;

extern void resume_function(void);

static unsigned int *get_lvl1_page_table_va(void);
static int find_paddr(uint32_t paddr, const void *vaddr_start, unsigned int range, void **found_vaddr);
static int alloc_phycont(unsigned int size, unsigned int alignment,  SceUID *uid, void **addr);
static int load_file_phycont(const char *path, SceUID *uid, void **addr, unsigned int *size);
static void uart0_print(const char *str);

static tai_hook_ref_t SceSyscon_ksceSysconResetDevice_ref;
static SceUID SceSyscon_ksceSysconResetDevice_hook_uid = -1;
static tai_hook_ref_t SceSyscon_ksceSysconSendCommand_ref;
static SceUID SceSyscon_ksceSysconSendCommand_hook_uid = -1;

static tai_hook_ref_t SceLowio_kscePervasiveUartResetEnable_ref;
static SceUID SceLowio_kscePervasiveUartResetEnable_hook_uid = -1;
static tai_hook_ref_t SceLowio_ScePervasiveForDriver_81A155F1_ref;
static SceUID SceLowio_ScePervasiveForDriver_81A155F1_hook_uid = -1;

static SceSysconResumeContext resume_ctx;
static uintptr_t resume_ctx_paddr;
static unsigned int resume_ctx_buff[32];
/*
 * Global variables used by the resume function.
 */
uintptr_t linux_paddr;
uintptr_t dtb_paddr;
void *lvl1_pt_va;

static void setup_payload(void)
{
	memset(&resume_ctx, 0, sizeof(resume_ctx));
	resume_ctx.size = sizeof(resume_ctx);
	resume_ctx.buff_vaddr = (unsigned int )resume_ctx_buff;
	resume_ctx.resume_func_vaddr = (unsigned int)&resume_function;
	asm volatile("mrc p15, 0, %0, c1, c0, 0\n\t" : "=r"(resume_ctx.SCTLR));
	asm volatile("mrc p15, 0, %0, c1, c0, 1\n\t" : "=r"(resume_ctx.ACTLR));
	asm volatile("mrc p15, 0, %0, c1, c0, 2\n\t" : "=r"(resume_ctx.CPACR));
	asm volatile("mrc p15, 0, %0, c2, c0, 0\n\t" : "=r"(resume_ctx.TTBR0));
	asm volatile("mrc p15, 0, %0, c2, c0, 1\n\t" : "=r"(resume_ctx.TTBR1));
	asm volatile("mrc p15, 0, %0, c2, c0, 2\n\t" : "=r"(resume_ctx.TTBCR));
	asm volatile("mrc p15, 0, %0, c3, c0, 0\n\t" : "=r"(resume_ctx.DACR));
	asm volatile("mrc p15, 0, %0, c10, c2, 0\n\t" : "=r"(resume_ctx.PRRR));
	asm volatile("mrc p15, 0, %0, c10, c2, 1\n\t" : "=r"(resume_ctx.NMRR));
	asm volatile("mrc p15, 0, %0, c12, c0, 0\n\t" : "=r"(resume_ctx.VBAR));
	asm volatile("mrc p15, 0, %0, c13, c0, 1\n\t" : "=r"(resume_ctx.CONTEXTIDR));
	asm volatile("mrc p15, 0, %0, c13, c0, 2\n\t" : "=r"(resume_ctx.TPIDRURW));
	asm volatile("mrc p15, 0, %0, c13, c0, 3\n\t" : "=r"(resume_ctx.TPIDRURO));
	asm volatile("mrc p15, 0, %0, c13, c0, 4\n\t" : "=r"(resume_ctx.TPIDRPRW));
	resume_ctx.time = ksceKernelGetSystemTimeWide();

	ksceKernelCpuDcacheAndL2WritebackRange(&resume_ctx, sizeof(resume_ctx));

	lvl1_pt_va = get_lvl1_page_table_va();

	LOG("Level 1 page table virtual address: %p\n", lvl1_pt_va);
}

static int ksceSysconResetDevice_hook_func(int type, int mode)
{
	LOG("ksceSysconResetDevice(0x%08X, 0x%08X)\n", type, mode);

	/*
	 * The Vita OS thinks it's about to poweroff, but we will instead
	 * setup the payload and trigger a soft reset.
	 */
	if (type == SCE_SYSCON_RESET_TYPE_POWEROFF) {
		setup_payload();
		type = SCE_SYSCON_RESET_TYPE_SOFT_RESET;
	}

	LOG("Resetting the device!\n");

	ksceKernelCpuDcacheWritebackInvalidateAll();
	ksceKernelCpuIcacheInvalidateAll();

	return TAI_CONTINUE(int, SceSyscon_ksceSysconResetDevice_ref, type, mode);
}

static int ksceSysconSendCommand_hook_func(int cmd, void *buffer, unsigned int size)
{
	LOG("ksceSysconSendCommand(0x%08X, %p, 0x%08X)\n", cmd, buffer, size);

	/*
	 * Change the resume context to ours.
	 */
	if (cmd == SCE_SYSCON_CMD_RESET_DEVICE && size == 4)
		buffer = &resume_ctx_paddr;

	return TAI_CONTINUE(int, SceSyscon_ksceSysconSendCommand_ref, cmd, buffer, size);
}

static int kscePervasiveUartResetEnable_hook_func(int uart_bus)
{
	/*
	 * We want to keep the UART enabled...
	 */
	return 0;
}

/*
 * Returns ScePervasiveMisc vaddr, ScePower uses it to disable the UART
 * by writing 0x80000000 to the word 0x20 bytes past the return value.
 */
static void *ScePervasiveForDriver_81A155F1_hook_func(void)
{
	static unsigned int tmp[0x24 / 4];
	LOG("ScePervasiveForDriver_81A155F1()\n");
	return tmp;
}

int module_start(SceSize argc, const void *args)
{
	int ret;
	char *dtb;
	SceUID linux_uid;
	SceUID dtb_uid;
	void *linux_vaddr;
	void *dtb_vaddr;
	unsigned int linux_size;
	unsigned int dtb_size;

	kscePervasiveUartClockEnable(0);
	kscePervasiveUartResetDisable(0);

	ksceUartInit(0);

	LOG("Linux loader by xerpi\n");

	/*
	 * Load the Linux files (kernel image and device tree blob).
	 */
	ret = load_file_phycont(ZIMAGE, &linux_uid, &linux_vaddr, &linux_size);
	if (ret < 0) {
		LOG("Error loading " ZIMAGE ": 0x%08X\n", ret);
		goto error_load_linux_image;
	}

	ksceKernelGetPaddr(linux_vaddr, &linux_paddr);

	LOG("Linux image: '%s'\n", ZIMAGE);
	LOG("Linux memory UID: 0x%08X\n", linux_uid);
	LOG("Linux load vaddr: 0x%08X\n", (unsigned int)linux_vaddr);
	LOG("Linux load paddr: 0x%08X\n", linux_paddr);
	LOG("Linux size: 0x%08X\n", linux_size);
	LOG("\n");

	if (ksceSblAimgrIsVITA()) {
		dtb = VITA1000_DTB;
	} else if (0) {
		dtb = VITA2000_DTB;
	} else if (ksceSblAimgrIsDolce()) {
		dtb = PSTV_DTB;
	} else {
		LOG("Unsupported Vita model.\n");
		goto error_load_dtb;
	}

	ret = load_file_phycont(dtb, &dtb_uid, &dtb_vaddr, &dtb_size);
	if (ret < 0) {
		LOG("Error loading %s: 0x%08X\n", dtb, ret);
		goto error_load_dtb;
	}

	ksceKernelGetPaddr(dtb_vaddr, &dtb_paddr);

	LOG("DTB: '%s'\n", dtb);
	LOG("DTB memory UID: 0x%08X\n", dtb_uid);
	LOG("DTB load vaddr: 0x%08X\n", (unsigned int)dtb_vaddr);
	LOG("DTB load paddr: 0x%08X\n", dtb_paddr);
	LOG("DTB size: 0x%08X\n", dtb_size);
	LOG("\n");

	SceSyscon_ksceSysconResetDevice_hook_uid = taiHookFunctionExportForKernel(KERNEL_PID,
		&SceSyscon_ksceSysconResetDevice_ref, "SceSyscon", 0x60A35F64,
		0x8A95D35C, ksceSysconResetDevice_hook_func);

	SceSyscon_ksceSysconSendCommand_hook_uid = taiHookFunctionExportForKernel(KERNEL_PID,
		&SceSyscon_ksceSysconSendCommand_ref, "SceSyscon", 0x60A35F64,
		0xE26488B9, ksceSysconSendCommand_hook_func);

	SceLowio_kscePervasiveUartResetEnable_hook_uid = taiHookFunctionExportForKernel(KERNEL_PID,
		&SceLowio_kscePervasiveUartResetEnable_ref, "SceLowio", 0xE692C727,
		0x788B6C61, kscePervasiveUartResetEnable_hook_func);

	SceLowio_ScePervasiveForDriver_81A155F1_hook_uid = taiHookFunctionExportForKernel(KERNEL_PID,
		&SceLowio_ScePervasiveForDriver_81A155F1_ref, "SceLowio", 0xE692C727,
		0x81A155F1, ScePervasiveForDriver_81A155F1_hook_func);

	LOG("Hooks installed.\n");

	ksceKernelGetPaddr(&resume_ctx, &resume_ctx_paddr);
	LOG("Resume context pa: 0x%08X\n", resume_ctx_paddr);

	LOG("Requesting standby...\n");

	kscePowerRequestStandby();

	return SCE_KERNEL_START_SUCCESS;

error_load_dtb:
	ksceKernelFreeMemBlock(linux_uid);
error_load_linux_image:
	return SCE_KERNEL_START_FAILED;
}

int module_stop(SceSize argc, const void *args)
{
	/*
	 * This is very important: it avoids the freeing
	 * of the resources allocated by the module.
	 */
	return SCE_KERNEL_STOP_CANCEL;
}

unsigned int *get_lvl1_page_table_va(void)
{
	uint32_t ttbcr;
	uint32_t ttbr0;
	uint32_t ttbcr_n;
	uint32_t lvl1_pt_pa;
	void *lvl1_pt_va;

	asm volatile(
		"mrc p15, 0, %0, c2, c0, 2\n\t"
		"mrc p15, 0, %1, c2, c0, 0\n\t"
		: "=r"(ttbcr), "=r"(ttbr0));

	ttbcr_n = ttbcr & 7;
	lvl1_pt_pa = ttbr0 & ~((1 << (14 - ttbcr_n)) - 1);

	if (!find_paddr(lvl1_pt_pa, (void *)0, 0xFFFFFFFF, &lvl1_pt_va))
		return NULL;

	return lvl1_pt_va;
}

int find_paddr(uint32_t paddr, const void *vaddr_start, unsigned int range, void **found_vaddr)
{
	const unsigned int step = 0x1000;
	void *vaddr = (void *)vaddr_start;
	const void *vaddr_end = vaddr_start + range;

	for (; vaddr < vaddr_end; vaddr += step) {
		uintptr_t cur_paddr;

		if (ksceKernelGetPaddr(vaddr, &cur_paddr) < 0)
			continue;

		if ((cur_paddr & ~(step - 1)) == (paddr & ~(step - 1))) {
			if (found_vaddr)
				*found_vaddr = vaddr;
			return 1;
		}
	}

	return 0;
}

int alloc_phycont(unsigned int size, unsigned int alignment, SceUID *uid, void **addr)
{
	int ret;
	SceUID mem_uid;
	void *mem_addr;

	SceKernelAllocMemBlockKernelOpt opt;
	memset(&opt, 0, sizeof(opt));
	opt.size = sizeof(opt);
	opt.attr = SCE_KERNEL_ALLOC_MEMBLOCK_ATTR_PHYCONT | SCE_KERNEL_ALLOC_MEMBLOCK_ATTR_HAS_ALIGNMENT;
	opt.alignment = ALIGN(alignment, 0x1000);
	mem_uid = ksceKernelAllocMemBlock("phycont", 0x30808006, ALIGN(size, 0x1000), &opt);
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

int load_file_phycont(const char *path, SceUID *uid, void **addr, unsigned int *size)
{
	int ret;
	SceUID fd;
	SceUID mem_uid;
	void *mem_addr;
	unsigned int file_size;
	unsigned int aligned_size;

	fd = ksceIoOpen(path, SCE_O_RDONLY, 0);
	if (fd < 0)
		return fd;

	file_size = ksceIoLseek(fd, 0, SCE_SEEK_END);
	aligned_size = ALIGN(file_size, 4096);

	ret = alloc_phycont(aligned_size, 4096, &mem_uid, &mem_addr);
	if (ret < 0) {
		ksceIoClose(fd);
		return ret;
	}

	ksceIoLseek(fd, 0, SCE_SEEK_SET);
	ksceIoRead(fd, mem_addr, file_size);

	ksceKernelCpuDcacheAndL2WritebackRange(mem_addr, aligned_size);
	ksceKernelCpuIcacheInvalidateRange(mem_addr, aligned_size);

	ksceIoClose(fd);

	if (uid)
		*uid = mem_uid;
	if (addr)
		*addr = mem_addr;
	if (size)
		*size = file_size;

	return 0;
}

static void uart0_print(const char *str)
{
	while (*str) {
		ksceUartWrite(0, *str);
		if (*str == '\n')
			ksceUartWrite(0, '\r');
		str++;
	}
}
