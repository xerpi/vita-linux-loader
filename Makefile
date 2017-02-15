TARGET   = linuxloader
TARGET_OBJS  = main.o log.o
PAYLOAD_OBJS = payload.o

LIBS = 	-lgcc -ltaihenForKernel_stub -lSceSysclibForDriver_stub -lSceSysmemForDriver_stub \
	-lSceSysmemForKernel_stub -lSceThreadmgrForKernel_stub -lSceThreadmgrForDriver_stub \
	-lSceIofilemgrForDriver_stub -lSceKernelSuspendForDriver_stub -lSceModulemgrForDriver_stub \
	-lSceModulemgrForKernel_stub -lSceDisplayForDriver_stub -lSceCpuForKernel_stub -lSceCpuForDriver_stub

PREFIX  = arm-vita-eabi
CC      = $(PREFIX)-gcc
AS      = $(PREFIX)-as
OBJCOPY = $(PREFIX)-objcopy
CFLAGS  = -Wl,-q -Wall -O0 -nostartfiles -mcpu=cortex-a9
ASFLAGS =

all: $(TARGET).skprx

%.skprx: %.velf
	vita-make-fself $< $@

%.velf: %.elf
	vita-elf-create -e $(TARGET).yml $< $@

payload.elf: $(PAYLOAD_OBJS)
	$(CC) -T payload.ld -nostartfiles $^ -o $@

payload.bin: payload.elf
	$(OBJCOPY) -S -O binary $^ $@

payload_bin.o: payload.bin
	$(OBJCOPY) --input binary --output elf32-littlearm \
		--binary-architecture arm $^ $@

$(TARGET).elf: $(TARGET_OBJS) payload_bin.o
	$(CC) $(CFLAGS) $^ $(LIBS) -o $@

clean:
	@rm -rf $(TARGET).skprx $(TARGET).velf $(TARGET).elf $(TARGET_OBJS) \
		payload.elf payload.bin payload_bin.o $(PAYLOAD_OBJS)


send: $(TARGET).skprx
	curl -T $(TARGET).skprx ftp://$(PSVITAIP):1337/ux0:/data/tai/kplugin.skprx
	@echo "Sent."
