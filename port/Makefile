PROJ = main
CPU ?= cortex-m4
FREERTOS_PATH = ../FreeRTOS-Kernel

CFLAGS = \
	-mcpu=$(CPU) \
	-mthumb \
	-mfpu=fpv4-sp-d16 \
	-mfloat-abi=hard \
	-ffreestanding \
	-DSTM32F401xE \
	-g

LDFLAGS = \
	-T map.ld \
	-nostartfiles

OBJ = boot.o \
	  start.o \
	  $(FREERTOS_PATH)/tasks.o \
	  $(FREERTOS_PATH)/list.o \
	  $(FREERTOS_PATH)/portable/MemMang/heap_4.o \
	  $(FREERTOS_PATH)/portable/GCC/ARM_CM4F/port.o

INC = -I$(FREERTOS_PATH)/include/ \
	  -I$(FREERTOS_PATH)/portable/GCC/ARM_CM4F \
	  -I./Drivers/CMSIS/Include \
      -I./Drivers/CMSIS/Device/ST/STM32F4xx/Include \
	  -I./

OPENOCD_INTERFACE ?= interface/stlink.cfg
OPENOCD_TARGET ?= target/stm32f4x.cfg

.PHONY: all flash debug clean
all: $(PROJ).elf

gdbserver:
	openocd -f $(OPENOCD_INTERFACE) -f $(OPENOCD_TARGET)

flash: $(PROJ).elf
	arm-none-eabi-gdb -nx -batch $(PROJ).elf \
		-ex "target extended-remote localhost:3333" \
		-ex "monitor reset halt" \
		-ex "load" \
		-ex "monitor reset" \
		-ex "quit"


debug: $(PROJ).elf
	gdb-multiarch $(PROJ).elf \
		-ex "target extended-remote localhost:3333" \
		-ex "monitor reset halt" \
		-ex "load" \
		-ex "break start" \
		-ex "continue"

%.o: %.S
	arm-none-eabi-gcc $(CFLAGS) -c $< -o $@

%.o: %.c
	arm-none-eabi-gcc $(INC) $(CFLAGS) -c $< -o $@

$(PROJ).elf: $(OBJ)
	arm-none-eabi-gcc $(CFLAGS) $(LDFLAGS) $^ -o $@
	arm-none-eabi-objdump -D -S $@ > $@.lst
	arm-none-eabi-readelf -a $@ > $@.debug

clean:
	find .. -name "*.o" -delete
	rm -f *.o *.elf *.lst *.debug
