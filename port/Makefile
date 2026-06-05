PROJ := main
CPU  ?= cortex-m4

CC      := arm-none-eabi-gcc
CXX     := arm-none-eabi-g++
AS      := arm-none-eabi-gcc
LD      := arm-none-eabi-g++
OBJDUMP := arm-none-eabi-objdump
READELF := arm-none-eabi-readelf

FREERTOS_PATH := ../FreeRTOS-Kernel

CFLAGS := \
	-mcpu=$(CPU)            \
	-mthumb                 \
	-mfpu=fpv4-sp-d16       \
	-mfloat-abi=hard        \
	-ffreestanding          \
	-Wall                   \
	-Wextra                 \
	-g                      \
	-DSTM32F401xE

CXXFLAGS := $(CFLAGS)               \
	-std=c++20                      \
	-fno-exceptions                 \
	-fno-rtti                       \
	-fno-unwind-tables              \
	-fno-asynchronous-unwind-tables

LDFLAGS := \
	-T map.ld    \
	-nostartfiles

INC := \
	-I$(FREERTOS_PATH)/include                              \
	-I$(FREERTOS_PATH)/portable/GCC/ARM_CM4F               \
	-I./Drivers/CMSIS/Include                              \
	-I./Drivers/CMSIS/Device/ST/STM32F4xx/Include          \
	-I.

SRC_C := \
	$(FREERTOS_PATH)/tasks.c                        \
	$(FREERTOS_PATH)/list.c                         \
	$(FREERTOS_PATH)/portable/MemMang/heap_4.c      \
	$(FREERTOS_PATH)/portable/GCC/ARM_CM4F/port.c

SRC_CPP := \
	start.cpp

SRC_ASM := \
	boot.S

OBJ := \
	$(SRC_C:.c=.o)      \
	$(SRC_CPP:.cpp=.o)  \
	$(SRC_ASM:.S=.o)

OPENOCD_INTERFACE ?= interface/stlink.cfg
OPENOCD_TARGET    ?= target/stm32f4x.cfg

.PHONY: all clean flash debug gdbserver

all: $(PROJ).elf

$(PROJ).elf: $(OBJ)
	$(LD) $(CXXFLAGS) $(LDFLAGS) $^ -o $@
	$(OBJDUMP) -D -S $@ > $@.lst
	$(READELF) -a $@ > $@.debug

%.o: %.c
	$(CC) $(INC) $(CFLAGS) -c $< -o $@

%.o: %.cpp
	$(CXX) $(INC) $(CXXFLAGS) -c $< -o $@

%.o: %.S
	$(AS) $(CFLAGS) -c $< -o $@

gdbserver:
	openocd -f $(OPENOCD_INTERFACE) -f $(OPENOCD_TARGET)

flash: $(PROJ).elf
	arm-none-eabi-gdb -nx -batch $(PROJ).elf       \
		-ex "target extended-remote localhost:3333" \
		-ex "monitor reset halt"                   \
		-ex "load"                                 \
		-ex "monitor reset"                        \
		-ex "quit"

debug: $(PROJ).elf
	gdb-multiarch $(PROJ).elf                      \
		-ex "target extended-remote localhost:3333" \
		-ex "monitor reset halt"                   \
		-ex "load"                                 \
		-ex "break main"                           \
		-ex "continue"

clean:
	find . -name "*.o" -delete
	find $(FREERTOS_PATH) -name "*.o" -delete
	rm -f $(PROJ).elf $(PROJ).elf.lst $(PROJ).elf.debug
