# Bare-Metal STM32 C++ Build Issues: Root Causes and Fixes

A detailed breakdown of every linker and runtime issue encountered building a C++ FreeRTOS project for the STM32F401, why each happened, and how it was resolved.

---

## Table of Contents

1. [Background: The Toolchain Stack](#1-background-the-toolchain-stack)
2. [Issue: `__dso_handle` undefined](#2-issue-__dso_handle-undefined)
3. [Issue: `__exidx_start` / `__exidx_end` undefined](#3-issue-__exidx_start--__exidx_end-undefined)
4. [Issue: Newlib syscall stubs (`_exit`, `_sbrk`, `_kill`, etc.)](#4-issue-newlib-syscall-stubs)
5. [Issue: `end` symbol undefined (`_sbrk` from nosys.specs)](#5-issue-end-symbol-undefined)
6. [Issue: Hardware not initializing (GPIO pull-up not working)](#6-issue-hardware-not-initializing-gpio-pull-up-not-working)
7. [Summary of All Fixes](#7-summary-of-all-fixes)

---

## 1. Background: The Toolchain Stack

Before diving into the issues, it helps to understand what each layer of the toolchain contributes.

### arm-none-eabi-g++

This is a **cross-compiler** — it runs on your Linux/Mac/Windows machine but produces code for an ARM Cortex-M4 target. The `none` means no OS, `eabi` means Embedded ABI.

### newlib

`arm-none-eabi-g++` ships with **newlib**, a C standard library implementation designed for embedded systems. It provides `printf`, `malloc`, `abort`, and so on. However, newlib's higher-level functions ultimately need OS services (file I/O, process control, memory) which it calls through a layer of **syscall stubs**: `_write`, `_read`, `_sbrk`, `_exit`, etc. On a real OS these call into the kernel. On bare-metal, you must provide them yourself or use a pre-built stub set.

### libgcc

`libgcc` is a helper library the compiler links automatically. It provides software implementations of things the hardware may not support natively (e.g. 64-bit division, floating-point emulation) and also contains the **ARM exception unwind tables** used for C++ exception handling and stack unwinding.

### The Linker Script

The linker script (`map.ld`) tells the linker how to arrange all the compiled object sections (`.text`, `.data`, `.bss`) into the target's memory map (FLASH and RAM). It also defines symbols like `_sdata`, `_ebss` that the startup code uses to initialize memory.

---

## 2. Issue: `__dso_handle` undefined

### Error

```
undefined reference to `__dso_handle'
/home/.../src/start.cpp:92:(.text+0x2bc): undefined reference to `__dso_handle'
main.elf: hidden symbol `__dso_handle' isn't defined
```

### What is `__dso_handle`?

`__dso_handle` stands for **Dynamic Shared Object handle**. It is a symbol used by the C++ runtime's **`__cxa_atexit`** mechanism.

In standard C++, when a static object with a non-trivial destructor is created, the runtime registers the destructor to be called when the program exits, via `atexit()`. The C++ ABI uses `__cxa_atexit` for this, which takes a pointer to the destructor, the object, and `__dso_handle` (a handle identifying which shared library the object belongs to, for proper unloading order). In a hosted environment, `__dso_handle` is defined by the C runtime startup code (`crt0`). On bare-metal with `-nostartfiles`, there is no `crt0`, so `__dso_handle` is never defined.

### Why did we trigger it?

Any static object with a non-trivial constructor/destructor triggers `__cxa_atexit`. This includes:

**Local statics:**
```cpp
extern "C" void start() {
    static GPIO led(GPIOC_BASE, 10, GPIO::Mode::Output); // triggers __cxa_atexit
}
```

**Global statics (also triggers it):**
```cpp
static GPIO led(GPIOC_BASE, 10, GPIO::Mode::Output); // also triggers __cxa_atexit
```

Both cases cause the compiler to emit a call to `__cxa_atexit` to register the `GPIO` destructor for when the program "exits." On bare-metal the program never exits, making this completely pointless — but the compiler doesn't know that.

### How it was fixed

Add `-fno-use-cxa-atexit` to `CXXFLAGS` in the Makefile:

```makefile
CXXFLAGS += -fno-use-cxa-atexit
```

This flag tells the compiler to **not emit `__cxa_atexit` calls** for static destructors. The destructor code still exists in the binary, but it will simply never be registered or called — which is exactly correct on bare-metal since the program never terminates.

### Why not just define `__dso_handle`?

You could stub it out:
```c
void* __dso_handle = nullptr;
```
But that only addresses the symbol; you'd still pull in `__cxa_atexit` which has other dependencies. `-fno-use-cxa-atexit` eliminates the call entirely, which is the correct approach for bare-metal.

---

## 3. Issue: `__exidx_start` / `__exidx_end` undefined

### Error

```
undefined reference to `__exidx_start'
undefined reference to `__exidx_end'
```

### What is `.ARM.exidx`?

`.ARM.exidx` is the **ARM Exception Index section**. It is part of the ARM EHABI (Exception Handling Application Binary Interface) — the mechanism that supports C++ exceptions and stack unwinding on ARM. Each compiled translation unit that can throw an exception has a corresponding `.ARM.exidx` section containing a compact unwind table.

Even when you compile with `-fno-exceptions`, **libgcc's `unwind-arm.o`** (already compiled without that flag) still references `__exidx_start` and `__exidx_end`. These are symbols the linker is expected to define at the start and end of the collected `.ARM.exidx` sections, so the unwinder knows where to search.

### Why did we hit this?

FreeRTOS pulls in `abort()` (used in `configASSERT`), which pulls in parts of newlib's C++ runtime, which pulls in `unwind-arm.o` from libgcc. Even though our own code used `-fno-exceptions` and `-fno-unwind-tables`, those flags only prevent *our* code from generating unwind tables — they cannot retroactively strip the already-compiled libgcc.

### How it was fixed

Add the `.ARM.exidx` section to the linker script so the linker can place those sections and define the boundary symbols:

```ld
.ARM.exidx : {
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
} > FLASH
```

This tells the linker: "collect all `.ARM.exidx*` input sections here, and define `__exidx_start` at the beginning and `__exidx_end` at the end." Even if there are no actual unwind entries (because `-fno-exceptions` was used), the section exists with zero size and the symbols are valid — the unwinder finds an empty table and does nothing.

---

## 4. Issue: Newlib Syscall Stubs

### Error

```
undefined reference to `_exit'
undefined reference to `_kill'
undefined reference to `_getpid'
undefined reference to `_write'
undefined reference to `_read'
undefined reference to `_close'
undefined reference to `_lseek'
undefined reference to `_sbrk'
undefined reference to `_fini'
```

### What are these?

These are newlib's **system call retargeting stubs**. Newlib is designed to be portable: its higher-level functions call these low-level stubs, which you are expected to implement for your specific platform. On Linux they'd call `syscall()`. On bare-metal, you must provide your own versions.

| Symbol | Purpose |
|--------|---------|
| `_exit` | Called by `abort()` and program termination |
| `_kill` | Send signal to process (used by `abort()` path) |
| `_getpid` | Get current process ID (used by signal handling) |
| `_write` | Write to file descriptor (used by `printf` etc.) |
| `_read` | Read from file descriptor |
| `_close` | Close file descriptor |
| `_lseek` | Seek within file |
| `_sbrk` | Grow the heap (used by `malloc`) |
| `_fini` | Called during program finalization |

### Why were they pulled in?

The chain was:

```
FreeRTOS configASSERT → abort() → _kill, _getpid, _exit
heap_4.c uses malloc/free internals → _sbrk
-nostartfiles omits crt0 which normally defines _fini → _fini missing
```

Even though our application code doesn't use `printf` or file I/O, `abort()` triggers signal-handling code in newlib which needs `_kill` and `_getpid`, and ultimately `_exit`.

### How it was fixed

Add `-specs=nosys.specs` to the linker flags:

```makefile
LDFLAGS += -specs=nosys.specs
```

`nosys.specs` is a **spec file** shipped with `arm-none-eabi-gcc` that links in `libnosys.a` — a pre-built library of minimal stub implementations that return error codes or loop forever. It provides all the required symbols in one shot without writing them manually:

- `_exit` → infinite loop
- `_kill`, `_getpid` → return -1
- `_write`, `_read`, `_close`, `_lseek` → return -1
- `_sbrk` → minimal heap bump allocator (needs `end` symbol — see next issue)

The linker warnings like `_close is not implemented and will always fail` are informational and expected — they confirm the stubs are being used.

### Why not `-specs=nano.specs` or `-specs=rdimon.specs`?

- `nano.specs` → links a smaller newlib-nano but still needs syscall stubs
- `rdimon.specs` → uses semihosting (JTAG-based I/O), requires a debugger connected
- `nosys.specs` → pure stubs, no dependencies, correct for standalone bare-metal

---

## 5. Issue: `end` Symbol Undefined

### Error

```
undefined reference to `end'
(from nosys.a sbrk.c)
```

### What is `end`?

`nosys.specs`'s `_sbrk` implementation uses a symbol called `end` as the **start of the heap**. It's a conventional linker symbol (also used by many other embedded toolchains) that marks where free RAM begins — i.e., where dynamic allocation can start growing upward from.

The `_sbrk` implementation looks roughly like:

```c
extern char end; // defined by linker script
static char* heap_ptr = &end;

void* _sbrk(int incr) {
    char* prev = heap_ptr;
    heap_ptr += incr;
    return prev;
}
```

Without `end` defined, the linker has no symbol to reference and fails.

### How it was fixed

Add `end` to the linker script immediately after `.bss` (so it points to the first free RAM address after static data):

```ld
.bss : ALIGN(4) {
    _sbss = .;
    *(.bss*)
    . = ALIGN(4);
    _ebss = .;
} > RAM

end = _ebss;  /* heap start, required by nosys _sbrk */
```

This makes `end` point to the byte immediately after `.bss`, which is correct: static variables live in `.data` and `.bss`, and the heap grows upward from there. The FreeRTOS heap (`heap_4.c`) manages its own pool anyway, so `_sbrk` is mostly there to satisfy `abort()` → `malloc` internal paths and will rarely be called.

---

## 6. Issue: Hardware Not Initializing (GPIO Pull-up Not Working)

### Symptom

PC12 configured as input with pull-up, but no voltage difference visible between the pin and GND.

### Root Cause: Global Constructors Run Before `start()`

This was a pure bare-metal C++ lifecycle issue. The code had:

```cpp
// Global scope
static GPIO led(GPIOC_BASE, 10, GPIO::Mode::Output);
static GPIO button(GPIOC_BASE, 12, GPIO::Mode::Input,
                   GPIO::OutputType::None,
                   GPIO::Speed::None,
                   GPIO::Pull::PullUp);

extern "C" void start() {
    init_mem();    // copies .data, zeros .bss
    clock_init();  // configures PLL, sets flash wait states
    ...
}
```

The problem is the **order of initialization**:

```
Power on
    → boot.S reset_handler runs
        → C++ runtime walks .init_array           ← Global constructors run HERE
            → GPIO::GPIO() calls enableClock()
            → RCC->AHB1ENR |= RCC_AHB1ENR_GPIOCEN  ← Clock write happens HERE
            → port()->PUPDR |= PullUp bits           ← Register write happens HERE
        → bl start
            → init_mem()   ← .data/.bss init, too late
            → clock_init() ← PLL setup, too late
```

The GPIO constructors fire during `.init_array` processing, **before `start()` is ever called**. At that point:

- `.data` section hasn't been copied from flash to RAM yet — so any global variables used in the constructor may have garbage values
- The system clock is still running on the default 16 MHz HSI (no PLL), which is actually fine for register writes
- More critically: the RCC peripheral clock enable write to `AHB1ENR` may work, but if `.data` isn't initialized, the `inuse` array (which is in `.bss`) hasn't been zeroed yet — so the constructor's guard check `if (inuse[_inuseIdx][_pin])` reads uninitialized memory and may falsely think the pin is already in use, returning early without configuring the pin

### How it was fixed

Move hardware objects off the global scope and construct them explicitly after all initialization is complete:

```cpp
static GPIO* led    = nullptr;  // trivially initialized pointer, no constructor
static GPIO* button = nullptr;

extern "C" void start() {
    init_mem();    // .data and .bss now valid
    clock_init();  // clocks up

    // Construct AFTER everything is ready
    led    = new GPIO(GPIOC_BASE, 10, GPIO::Mode::Output);
    button = new GPIO(GPIOC_BASE, 12, GPIO::Mode::Input,
                      GPIO::OutputType::None,
                      GPIO::Speed::None,
                      GPIO::Pull::PullUp);

    button->setInterruptCallback(GPIO::Edge::Fall, on_button_press, led);
    ...
}
```

Raw pointers initialized to `nullptr` are **trivially initialized** — the compiler emits no constructor call, no `.init_array` entry, no `__cxa_atexit` registration. They're just zero-initialized as part of `.bss`.

`new` here uses FreeRTOS's `heap_4` allocator (since `heap_4.c` overrides `operator new` via `pvPortMalloc`), so there's no dependency on the newlib heap either.

### The General Rule

> **Never access hardware registers from global constructors on bare-metal.**

The safe initialization order is always:
1. Copy `.data` from flash to RAM
2. Zero `.bss`
3. Configure clocks and flash wait states
4. Then initialize all hardware peripherals

Global C++ constructors violate this order because they run as part of the C++ runtime startup, which happens before your `main()` / `start()` function body executes.

---

## 7. Summary of All Fixes

| Issue | Root Cause | Fix |
|-------|-----------|-----|
| `__dso_handle` undefined | Compiler emits `__cxa_atexit` calls for static object destructors; symbol only exists in hosted `crt0` | Add `-fno-use-cxa-atexit` to `CXXFLAGS` |
| `__exidx_start/end` undefined | libgcc's `unwind-arm.o` references these boundary symbols; linker script had no `.ARM.exidx` section | Add `.ARM.exidx` section with boundary symbols to `map.ld` |
| Newlib syscall stubs missing | `abort()` and malloc paths in newlib require OS retargeting stubs; `-nostartfiles` doesn't provide them | Add `-specs=nosys.specs` to `LDFLAGS` |
| `end` symbol undefined | `nosys.specs`'s `_sbrk` uses `end` as heap start pointer; not defined in linker script | Add `end = _ebss;` to `map.ld` after `.bss` |
| GPIO pull-up not working | Global C++ constructors run before `init_mem()`/`clock_init()`, so `.bss` is uninitialized when `GPIO` constructor fires | Use pointer globals (`GPIO* = nullptr`) and construct with `new` after full initialization in `start()` |

### Final Makefile flags that matter

```makefile
CXXFLAGS += -fno-use-cxa-atexit      # No __cxa_atexit, no __dso_handle
CXXFLAGS += -fno-exceptions           # Don't generate exception tables in our code
CXXFLAGS += -fno-unwind-tables        # Don't generate unwind info in our code
LDFLAGS  += -specs=nosys.specs        # Provide newlib syscall stubs
LDFLAGS  += -nostartfiles             # Don't link crt0 (we have our own boot.S)
```

### Final linker script additions

```ld
/* Needed by libgcc unwind-arm.o */
.ARM.exidx : {
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
} > FLASH

/* Needed by nosys.specs _sbrk */
end = _ebss;
```
