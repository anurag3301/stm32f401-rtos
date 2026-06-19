# Debugging: Silent Crash with Multiple Tasks Calling printf

## The Symptom

Adding a third FreeRTOS task (`vTask3`) that called `printf` caused the system to silently stop producing any UART output. No LED indication, no crash message — just silence. `vTask1` (GPIO toggle) and `vTask2` (periodic printf) worked fine alone.

## First Suspect: Stack Overflow

The natural first guess for a silent FreeRTOS crash is stack overflow. Each task was created with `configMINIMAL_STACK_SIZE = 0x100`. `printf` on newlib needs several hundred bytes of stack for internal formatting buffers, so it seemed plausible that adding a third printf-calling task pushed something over the edge.

To get visibility, we:
- Added a bare-register GPIO init for PC13 at the very start of `start()` (before `init_mem`, so it works even if something breaks during startup)
- Wrote a `fault_handler` that drives PC13 low and loops, wired to all fault vectors in `boot.S` (NMI, HardFault, MemManage, BusFault, UsageFault)
- Enabled FreeRTOS stack overflow detection: `configCHECK_FOR_STACK_OVERFLOW 2` (method 2 fills the bottom of the stack with a sentinel pattern and checks it on every context switch)
- Added `vApplicationStackOverflowHook` → `fault_handler`

Manual test: deliberately calling a null function pointer triggered `fault_handler` correctly — PC13 went low. So the hardware path worked.

But with `vTask3` running, PC13 stayed high. No stack overflow detected.

## Second Suspect: Heap Exhaustion

`configMINIMAL_STACK_SIZE = 0x100` is in **words**, not bytes — 256 words = 1024 bytes. That's actually enough for printf. So the real question became: can FreeRTOS even allocate all the tasks?

Each task needs its TCB (~120 bytes) + stack (1024 bytes) + heap block overhead ≈ 1160 bytes. With 3 user tasks + the idle task, that's ~4640 bytes — but `configTOTAL_HEAP_SIZE` was only 4096 bytes. One or more tasks were silently failing to create. `xTaskCreate` returns an error but the code wasn't checking `xReturn`.

Added `configUSE_MALLOC_FAILED_HOOK 1` and `vApplicationMallocFailedHook` → `fault_handler`, and increased the heap to `0x4000` (16 KB). The chip has 64 KB SRAM so there was plenty of room.

## Still Crashing — Under GDB Only

With the larger heap, the system now ran, but crashed unpredictably — and only when running under GDB. In standalone (no debugger attached), it ran fine. Neither the stack overflow hook nor the malloc failed hook was hit before the crash. Something was triggering a real hardware fault vector.

## Decoding the Fault Registers

With a GDB breakpoint on `fault_handler`, we read the Cortex-M4 fault status registers:

```
CFSR  (0xE000ED28) = 0x00000001
HFSR  (0xE000ED2C) = 0x40000000
PSP                = 0x00000000
basepri            = 0x50
```

**CFSR = 0x00000001** — bit 0 of MMFSR is `IACCVIOL`: the CPU tried to fetch an instruction from an **XN (Execute Never) region**. Without an MPU, XN regions are defined by the default Cortex-M4 memory map: peripherals (0x40000000–0x5FFFFFFF) and the system control space (0xE0000000+) are both XN.

**HFSR = 0x40000000** — the `FORCED` bit: the MemManage fault escalated to HardFault because MemManage isn't explicitly enabled in `SCB->SHCSR`.

**PSP = 0x0** — the Process Stack Pointer was zero at fault time. In a running FreeRTOS system, PSP always points to the current task's stack. A PSP of zero means it was either never initialised, or — more likely — had been corrupted.

**basepri = 0x50** — this is `configMAX_SYSCALL_INTERRUPT_PRIORITY` (5 << 4), meaning we were inside a FreeRTOS critical section at the time of the fault. This rules out an early-startup fault; it happened inside running task code.

## What Actually Happened

Putting it together: the fault happened during a **PendSV context switch** loading a corrupted task stack frame.

newlib's `printf` is not thread-safe by default. It uses a single global `_impure_ptr` that points to a shared `struct _reent` containing all internal FILE state (buffers, format state, etc.). When two tasks call `printf` and the scheduler preempts one mid-call, the other task corrupts that shared state — overwriting function pointers or buffer addresses with garbage values.

The corrupted state eventually produced a garbage return address or function pointer pointing into the peripheral address space (XN). At the next context switch, PendSV saved this corrupted stack frame. When that task was scheduled again, PendSV restored the corrupted frame, set the restored garbage address as PC, and the CPU tried to fetch an instruction from an XN address → IACCVIOL → FORCED HardFault.

The reason it only manifested under GDB: the SWD interface slows task execution, changing the interleaving of context switches. This happened to hit the printf race condition in a way that standalone timing did not. The bug was always latent; GDB just exposed it sooner.

## The Fix

`configUSE_NEWLIB_REENTRANT 1` in `FreeRTOSConfig.h`.

This adds a private `struct _reent` inside each FreeRTOS task's TCB. On every context switch, FreeRTOS swaps `_impure_ptr` to point to the current task's own reent struct. Each task gets completely independent FILE buffers, errno, format state, and all other newlib globals. There is no longer any shared state for concurrent printf calls to corrupt.

Note: `struct _reent` in full newlib is large (200–400 bytes). With multiple tasks each carrying their own copy, heap requirements increase significantly. The heap was raised to `0x8000` (32 KB) to accommodate this comfortably within the chip's 64 KB SRAM.

## Takeaways

- A silent "no output" symptom with multiple FreeRTOS tasks is not always a stack overflow — check heap exhaustion first by computing `n_tasks × (TCB + stack)` against `configTOTAL_HEAP_SIZE`.
- Fault registers tell you exactly what kind of fault occurred. IACCVIOL + FORCED + a corrupted PSP almost always means PendSV loaded a garbage stack frame — trace back to what corrupted the stack.
- newlib is not thread-safe by default. Any project using printf from multiple FreeRTOS tasks must either enable `configUSE_NEWLIB_REENTRANT` or protect printf with a mutex.
- Bugs that only appear under GDB are timing-dependent race conditions. The debugger is not lying — it's exposing latent concurrency issues that standalone timing happens to avoid.
