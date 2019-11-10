/*
 * Simple kplugin loader by xerpi
 *Linux theming and file checks by CreepNT
 */

#include <stdio.h>
#include <taihen.h>
#include <psp2/ctrl.h>
#include <psp2/io/fcntl.h>
#include "debugScreen.h"

#define printf(...) psvDebugScreenPrintf(__VA_ARGS__)

#define MOD_PATH "ux0:linux/baremetal-loader.skprx"
#define PAYLOAD_PATH "ux0:linux/payload.bin"

static void wait_start_press();
static void wait_cross_press();
static int are_keyfiles_present();

int main(int argc, char *argv[])
{
	int ret;
	SceUID mod_id;

	psvDebugScreenInit();

	printf("Plugin Loader by xerpi\n");
	printf("Linux theming and 'enhancements' by CreepNT\n");
	printf("(he's the one to blame if it breaks :p)\n\n");

	if (are_keyfiles_present() == 0)//A file required to run Linux is not present.
	{
	wait_start_press();//Tell user to press START to exit.
	return 0;
	}

	wait_cross_press();

	tai_module_args_t argg;
	argg.size = sizeof(argg);
	argg.pid = KERNEL_PID;
	argg.args = 0;
	argg.argp = NULL;
	argg.flags = 0;
	mod_id = taiLoadStartKernelModuleForUser(MOD_PATH, &argg);

	if (mod_id < 0)
		printf("Error loading " MOD_PATH ": 0x%08X\n", mod_id);
	else
		printf("Module loaded with ID: 0x%08X\n", mod_id);

	wait_start_press();

	if (mod_id >= 0) {
		tai_module_args_t argg;
		argg.size = sizeof(argg);
		argg.pid = KERNEL_PID;
		argg.args = 0;
		argg.argp = NULL;
		argg.flags = 0;
		ret = taiStopUnloadKernelModuleForUser(mod_id, &argg, NULL, NULL);
		printf("Stop unload module: 0x%08X\n", ret);
	}

	return 0;
}

void wait_start_press()
{
	SceCtrlData pad;
	printf("\nPress START to exit.\n");
	while (1) {
		sceCtrlPeekBufferPositive(0, &pad, 1);
		if (pad.buttons & SCE_CTRL_START)
			break;
		sceKernelDelayThread(200 * 1000);

	}
}

void wait_cross_press()
{	
	SceCtrlData pad;
	printf("Press X to load Linux bootstrapper.\n");
	while (1) {
		sceCtrlPeekBufferPositive(0, &pad, 1);
		if (pad.buttons & SCE_CTRL_CROSS)
			break;
		sceKernelDelayThread(200 * 1000);
	}
}


int are_keyfiles_present()
{
	int error_count = 0;

	//Checking for payload and kernel plugin on ux0:
	SceUID payload_fd = sceIoOpen(PAYLOAD_PATH, SCE_O_RDONLY|SCE_O_NBLOCK, 0777);
	SceUID bootstrap_fd = sceIoOpen(MOD_PATH, SCE_O_RDONLY|SCE_O_NBLOCK, 0777);
	if(payload_fd < 0)
	{printf("Error : " PAYLOAD_PATH " not detected!\n");
	error_count++;}
	if (bootstrap_fd < 0)
	{printf("Error : " MOD_PATH " not detected!\n");
	error_count++;}

	//Check done, we close our access to both files.
	sceIoClose(bootstrap_fd);
	sceIoClose(payload_fd);

	//We check for zImage and DTB on ux0: (because idk how to detect which partition M2MC is mounted on)
	//THOSE FILES MUST BE ON THE MEMORY CARD, WHATEVER ITS MOUNTING POINT IS
	//You can create dummy files if using an SD2Vita (use VitaShell)
	SceUID zImage_fd = sceIoOpen("ux0:linux/zImage", SCE_O_RDONLY|SCE_O_NBLOCK, 0777);
	SceUID DTB_fd = sceIoOpen("ux0:linux/vita.dtb", SCE_O_RDONLY|SCE_O_NBLOCK, 0777);

	if (zImage_fd < 0){
	printf("Error : ux0:linux/zImage not detected !\n");
	error_count++;}
	if (DTB_fd < 0)
	{printf("Error : ux0:linux/vita.dtb not detected !\n");
	error_count++;}

	//Check done, we close access.
	sceIoClose(zImage_fd);
	sceIoClose(DTB_fd);

	//if there was any error, report so.
	if (error_count != 0) {return 0;}
	//else
	printf("\nAll checks passed !\n");
	printf("\nWarning : if you're using an SD adapter i.e. SD2Vita,\n");
	printf("copy zImage and vita.dtb to the memory card,\n");
	printf("or your Vita will crash when booting Linux!\n\n");
	return 1;
}
