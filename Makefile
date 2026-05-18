PROJ = main
CPU ?= cortex-m4
OBJ = boot.o

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
		-ex "break reset_handler" \
		-ex "continue"

%.o: %.S
	arm-none-eabi-as -mcpu=$(CPU) -mthumb -g -c $< -o $@

%.o: %.c
	arm-none-eabi-gcc $(INC) -mcpu=$(CPU) -mthumb -g -c $< -o $@

$(PROJ).elf: $(OBJ)
	arm-none-eabi-ld -T map.ld $^ -o $@
	arm-none-eabi-objdump -D -S $@ > $@.lst
	arm-none-eabi-readelf -a $@ > $@.debug

clean:
	rm -f *.o *.elf *.lst *.debug
