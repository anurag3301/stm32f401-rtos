# FreeRTOS Debugging Report: `configMAX_SYSCALL_INTERRUPT_PRIORITY` and Slow Task Delays

**Platform:** STM32F401 (Cortex-M4)  
**RTOS:** FreeRTOS (FreeRTOS-Kernel, v10.x)  
**Symptom:** `vTaskDelay(pdMS_TO_TICKS(100))` producing ~10 second delays instead of 100ms

---

## 1. The Symptom

A simple two-task FreeRTOS application was built:

- **vTask1** — toggles a GPIO (LED blink) every `pdMS_TO_TICKS(100)` milliseconds
- **vTask2** — busy loop incrementing a counter

With `configTICK_RATE_HZ = 10`, the LED blinked at the expected 100ms interval — apparently correct. When `configTICK_RATE_HZ` was raised to 1000 (the standard value for 1ms tick resolution), the LED blink slowed dramatically to approximately 10 seconds per toggle.

---

## 2. Investigation Timeline

### Hypothesis 1: `configCPU_CLOCK_HZ` mismatch

`pdMS_TO_TICKS` is defined as:

```c
#define pdMS_TO_TICKS(ms) ((ms * configTICK_RATE_HZ) / 1000)
```

At `configTICK_RATE_HZ = 10`: `pdMS_TO_TICKS(100)` = 1 tick  
At `configTICK_RATE_HZ = 1000`: `pdMS_TO_TICKS(100)` = 100 ticks

The macro itself was correct. The suspicion shifted to whether the CPU was actually running at the configured frequency. `configCPU_CLOCK_HZ` is used by `xPortStartScheduler()` inside `port.c` to program the SysTick reload register:

```c
portNVIC_SYSTICK_LOAD_REG = (configCPU_CLOCK_HZ / configTICK_RATE_HZ) - 1UL;
```

If `configCPU_CLOCK_HZ = 84000000` but the CPU was still on the 16MHz HSI (PLL not switching correctly), SysTick would fire ~5× too slowly.

`configCPU_CLOCK_HZ` was changed to `16000000` to match HSI and `clock_init()` was simplified to skip the PLL entirely. The delay was still ~10 seconds. This ruled out a clock frequency mismatch as the root cause.

### Hypothesis 2: Linker script — corrupt `.data` init

`_sidata` (the flash load address of `.data`) was placed inside the `.data` section braces:

```ld
.data : ALIGN(4) {
    _sdata = .;
    _sidata = LOADADDR(.data);  /* ← inside braces */
    ...
} > RAM AT > FLASH
```

In some linker versions this evaluates to the RAM VMA rather than the flash LMA, meaning `init_mem()` would copy from the wrong address — corrupting FreeRTOS heap and internal state before `xTaskCreate` ever ran.

`_sidata` was moved outside:

```ld
} > RAM AT > FLASH
_sidata = LOADADDR(.data);  /* ← correct */
```

GDB confirmed both versions produced `_sidata = 0x08002c7c` (flash address) in this toolchain — the linker was resolving it correctly either way. Not the root cause, but the outside placement is more portable and correct by spec.

### Hypothesis 3: Heap exhaustion silently failing task creation

With `configTOTAL_HEAP_SIZE = 0x1000` (4KB), the memory budget per task is:

| Item | Size |
|---|---|
| TCB (Task Control Block) | ~160 bytes |
| Stack (`configMINIMAL_STACK_SIZE = 0x100`) | 1024 bytes |
| **Per task total** | **~1184 bytes** |

Two tasks + FreeRTOS idle task = ~3.5KB, nearly exhausting the 4KB heap. `xTaskCreate` was returning `errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY` but the return value was not being checked, so failure was silent and the scheduler ran with no tasks.

Heap was increased to `0x4000` (16KB). This helped confirm task creation was succeeding, but timing was still wrong. Not the root cause of the delay.

---

## 3. The Root Cause

The actual bug was a single line in the original config:

```c
#define configMAX_SYSCALL_INTERRUPT_PRIORITY    1
```

### Why this is wrong

The STM32F401 (Cortex-M4) implements **4 priority bits** in the NVIC, stored in the **top 4 bits** (bits [7:4]) of each 8-bit priority register. The bottom 4 bits are hardwired to zero and ignored by hardware.

FreeRTOS uses `configMAX_SYSCALL_INTERRUPT_PRIORITY` as a raw register value. It writes this directly into the SysTick and PendSV priority registers via:

```c
portNVIC_SHPR3_REG |= portNVIC_SYSTICK_PRI;   // SysTick priority
portNVIC_SHPR3_REG |= portNVIC_PENDSV_PRI;    // PendSV priority
```

With `configMAX_SYSCALL_INTERRUPT_PRIORITY = 1`, the value `0x01` is written into the priority register. But since hardware only reads the **top nibble**, `0x01` is seen as `0x00` — the **highest possible priority**.

This places SysTick and PendSV at priority 0. The Cortex-M4 architecture manual states:

> **SysTick must have lower priority than PendSV.** PendSV is used by FreeRTOS for context switching. If SysTick fires at equal or higher priority than PendSV, it can preempt a context switch mid-execution, corrupting the task stack and the tick counter.

The result: `xTickCount` increments incorrectly. Ticks are dropped or duplicated depending on the race. `vTaskDelay` blocks for the right number of ticks, but those ticks take far longer in wall time than they should — appearing as if the delay is 10–100× too long.

### The correct value

Priority values on CM4 with 4 bits implemented must be written into the **top nibble**:

```c
// Wrong — raw value 1, hardware reads as priority 0 (highest)
#define configMAX_SYSCALL_INTERRUPT_PRIORITY    1

// Correct — priority 5 shifted into top nibble
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY    5
#define configMAX_SYSCALL_INTERRUPT_PRIORITY \
    (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - 4))
// = 5 << 4 = 0x50
```

`0x50` written into the register is read by hardware as priority 5 — below the default priority 0 of any uninitialized interrupt, safely above priority 15 (lowest). SysTick and PendSV now operate at the intended priority level, the tick counter advances correctly, and `vTaskDelay` produces accurate delays.

The value 5 is used in ST's own FreeRTOS examples and is the standard recommendation for STM32F4 targets.

---

## 4. Why It "Worked" at `configTICK_RATE_HZ = 10`

At 10 Hz, each tick is 100ms. `pdMS_TO_TICKS(100)` = 1 tick. The task was delaying for exactly 1 tick — so even if the tick counter was being corrupted, the probability of it completing a single tick count before the race condition fired was high enough that the LED appeared to blink at ~100ms. It was working **by accident**, not by correctness.

At 1000 Hz, the task delays for 100 ticks. The accumulated error from the priority conflict multiplied across 100 ticks, making the real-world delay 10–100× longer than expected and exposing the bug.

---

## 5. The Fix

**Only two lines changed:**

```diff
- #define configMAX_SYSCALL_INTERRUPT_PRIORITY    1
+ #define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY    5
+ #define configMAX_SYSCALL_INTERRUPT_PRIORITY            ( configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - 4) )
```

All other changes (heap size, clock config, linker script, `extern "C"`, `INCLUDE_` guards) were either unrelated fixes to separate issues or confirmed-unnecessary investigations.

---

## 6. Lessons

**Always shift interrupt priorities into the top nibble on STM32.** The NVIC priority register is 8 bits wide but only the top N bits (4 on STM32F4) are implemented. A raw value of `1` is indistinguishable from `0` at the hardware level.

**`configMAX_SYSCALL_INTERRUPT_PRIORITY` is a raw register value, not a logical priority number.** Most FreeRTOS ports on ARM document this, but it is easy to miss and the consequences are non-obvious timing corruption rather than a hard fault.

**Check `xTaskCreate` return values from day one.** Silent heap exhaustion wasted significant debug time. Adding `configASSERT` to the config immediately catches allocation failures:

```c
#define configASSERT(x)  if((x) == 0){ __asm volatile("bkpt #0"); }
```

**Working by coincidence is not working.** The original `configTICK_RATE_HZ = 10` masked the bug entirely. Any time a timing-sensitive system "works" only at one specific configuration value, that is a strong signal that something is wrong at a deeper level.
