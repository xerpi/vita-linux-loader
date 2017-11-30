TARGET   = linuxloader
TARGET_OBJS  = main.o trampoline.o
PAYLOAD_OBJS = payload/start.o

LIBS =	-ltaihenForKernel_stub -lSceSysclibForDriver_stub -lSceSysmemForDriver_stub \
	-lSceSysmemForKernel_stub -lSceThreadmgrForDriver_stub -lSceCpuForKernel_stub \
	-lSceCpuForDriver_stub -lSceUartForKernel_stub -lScePervasiveForDriver_stub \
	-lSceSysconForDriver_stub -lScePowerForDriver_stub -lSceIofilemgrForDriver_stub

PREFIX  = arm-vita-eabi
CC      = $(PREFIX)-gcc
AS      = $(PREFIX)-as
OBJCOPY = $(PREFIX)-objcopy
CFLAGS  = -Wl,-q -Wall -O0 -nostartfiles -mcpu=cortex-a9 -mthumb-interwork
ASFLAGS =

all: $(TARGET).skprx

%.skprx: %.velf
	vita-make-fself -c $< $@

%.velf: %.elf
	vita-elf-create -e $(TARGET).yml $< $@

payload.elf: $(PAYLOAD_OBJS)
	$(CC) -T payload/payload.ld -nostartfiles -nostdlib $^ -o $@ -lgcc

payload.bin: payload.elf
	$(OBJCOPY) -S -O binary $^ $@

payload_bin.o: payload.bin
	$(OBJCOPY) --input binary --output elf32-littlearm \
		--binary-architecture arm $^ $@

$(TARGET).elf: $(TARGET_OBJS) payload_bin.o
	$(CC) $(CFLAGS) $^ $(LIBS) -o $@

.PHONY: all clean send

clean:
	@rm -rf $(TARGET).skprx $(TARGET).velf $(TARGET).elf $(TARGET_OBJS) \
		payload.elf payload.bin payload_bin.o $(PAYLOAD_OBJS)

send: $(TARGET).skprx
	curl --ftp-method nocwd -T $(TARGET).skprx ftp://$(PSVITAIP):1337/ux0:/data/tai/kplugin.skprx
	@echo "Sent."
