# f401-rtos

FreeRTOS on the STM32F401 with a custom linker script and bare-metal startup, no STM32Cube, no vendor HAL, just CMSIS headers and the FreeRTOS kernel.

## What's in here

| Path | Purpose |
|---|---|
| `src/boot.S` | Vector table + reset handler (branches to `start`) |
| `src/start.cpp` | `.data`/`.bss` init, PLL clock setup (84 MHz), calls `main()` |
| `map.ld` | Linker script, FLASH 512 K @ `0x08000000`, RAM 96 K @ `0x20000000` |
| `FreeRTOS-Kernel/` | FreeRTOS kernel submodule (CM4F port, heap_4) |
| `driver/` | `stm32f401-cpp-hal` submodule, see [driver/README.md](driver/README.md) |

The build uses C++20 with `-fno-exceptions -fno-rtti`, no runtime overhead from those features.

## Build

```bash
# clone with submodules
git clone --recurse-submodules <repo-url>

# build
make

# flash (requires openocd running: make gdbserver)
make flash

# debug
make gdbserver   # terminal 1
make debug       # terminal 2
```

**Toolchain:** `arm-none-eabi-g++`, OpenOCD + ST-Link

## Project structure

```
src/
  boot.S       # vector table, reset_handler, weak IRQ aliases
  start.cpp    # memory init, HSI PLL → 84 MHz, entry point
  main.cpp     # FreeRTOS tasks (application code)
  syscalls.cpp # minimal newlib syscall stubs (_write → UART)
map.ld         # custom linker script
driver/        # stm32f401-cpp-hal submodule (GPIO, UART)
FreeRTOS-Kernel/
include/       # FreeRTOSConfig.h and project headers
```
