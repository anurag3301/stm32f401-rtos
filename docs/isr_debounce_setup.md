# Button Debounce on STM32F4 with FreeRTOS: The Full Story

A detailed breakdown of every approach tried, why each failed or succeeded, and a deep dive into the final solution.

---

## Table of Contents

1. [What is Button Bounce?](#1-what-is-button-bounce)
2. [The Starting Point: Raw ISR Toggle](#2-the-starting-point-raw-isr-toggle)
3. [Attempt 1: Deferred Interrupt with a FreeRTOS Task](#3-attempt-1-deferred-interrupt-with-a-freertos-task)
4. [Why Attempt 1 Failed](#4-why-attempt-1-failed)
5. [Attempt 2: One-Shot Setup Task](#5-attempt-2-one-shot-setup-task)
6. [Why Attempt 2 Still Didn't Work](#6-why-attempt-2-still-didnt-work)
7. [The Final Solution: Timestamp Debounce in the ISR](#7-the-final-solution-timestamp-debounce-in-the-isr)
8. [Deep Dive: `xTaskGetTickCountFromISR`](#8-deep-dive-xtaskgettickcountfromisr)
9. [Deep Dive: Static Local Variables in ISRs](#9-deep-dive-static-local-variables-in-isrs)
10. [Deep Dive: Captureless Lambdas as Function Pointers](#10-deep-dive-captureless-lambdas-as-function-pointers)
11. [Pros and Cons of Every Approach](#11-pros-and-cons-of-every-approach)
12. [When to Use Which Approach](#12-when-to-use-which-approach)
13. [Summary](#13-summary)

---

## 1. What is Button Bounce?

A mechanical button is made of two metal contacts. When you press it, the contacts don't make a clean single connection — they physically bounce off each other several times before settling. This lasts anywhere from a few microseconds to around 20ms depending on the button quality.

```
Ideal press:
    ____
   |    |___________________________
   
Real press (bouncing):
    _ _ ___
   | | |   |__________________________
   ^ ^ ^
   bounces
```

To a microcontroller reading a GPIO pin, each of these bounces looks like a separate button press. At 84 MHz with interrupts, the STM32F4 can easily catch every single bounce — so one physical press of a button can trigger 5–20 interrupts in rapid succession, causing an LED to flicker or a counter to jump by multiple counts instead of one.

Debouncing is the process of filtering out these false edges and accepting only one event per physical press.

---

## 2. The Starting Point: Raw ISR Toggle

The working baseline before debouncing was attempted:

```cpp
static void on_button_press(void* param) {
    GPIO* led = static_cast<GPIO*>(param);
    led->toggle();
}

button->setInterruptCallback(GPIO::Edge::Fall, on_button_press, led);
```

This works — the LED toggles on button press — but every bounce also toggles the LED, so a single press might toggle it 3–10 times resulting in unpredictable behaviour. On a good day it looks like it works, on a bad day the LED ends up in the wrong state after a press.

---

## 3. Attempt 1: Deferred Interrupt with a FreeRTOS Task

The textbook FreeRTOS pattern for ISR debouncing is the **deferred interrupt** approach:

- The ISR does the absolute minimum: sends a notification to a task
- The task wakes up, waits a debounce period, checks the pin state, and acts

```cpp
static TaskHandle_t debounce_task_handle = nullptr;

static void debounce_task(void* param) {
    GPIO* led = static_cast<GPIO*>(param);
    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);  // block until ISR wakes us
        vTaskDelay(pdMS_TO_TICKS(50));             // wait for bounce to settle
        if (!button->get()) {                      // confirm pin is still low
            led->toggle();
        }
    }
}

button->setInterruptCallback(GPIO::Edge::Fall, +[](void*) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    vTaskNotifyGiveFromISR(debounce_task_handle, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}, nullptr);

xTaskCreate(debounce_task, "debounce", configMINIMAL_STACK_SIZE, led, 2, &debounce_task_handle);
```

### How `ulTaskNotifyTake` works

FreeRTOS Task Notifications are a lightweight alternative to semaphores. Every task has a built-in 32-bit notification value. `vTaskNotifyGiveFromISR` increments it; `ulTaskNotifyTake` decrements it (or clears it) and blocks if the value is zero.

With `pdTRUE` as the first argument, `ulTaskNotifyTake` clears the value to zero on exit — binary semaphore behaviour. This means if the button bounces 10 times while the task is in its 50ms delay, those 10 notifications accumulate as a count, but the next `ulTaskNotifyTake` clears it all in one shot and the task runs only once more. This is exactly right — multiple bounces collapse into a single debounced event.

### How `portYIELD_FROM_ISR` works

When `vTaskNotifyGiveFromISR` wakes up the debounce task, FreeRTOS sets `xHigherPriorityTaskWoken = pdTRUE` if the notified task has higher priority than the currently running task. `portYIELD_FROM_ISR` checks this and if true, triggers a context switch via PendSV immediately on ISR exit — so the debounce task runs right away rather than waiting for the next scheduler tick. This is important for responsiveness and is the correct pattern for all `FromISR` functions.

---

## 4. Why Attempt 1 Failed

The code compiled and flashed but the LED didn't respond at all. The root cause was a **race condition between interrupt arming and scheduler startup**.

The sequence in `start()` was:

```cpp
// 1. Task created — handle is populated
xTaskCreate(debounce_task, "debounce", ..., &debounce_task_handle);

// 2. Interrupt armed — ISR can now fire
button->setInterruptCallback(GPIO::Edge::Fall, +[](void*) {
    vTaskNotifyGiveFromISR(debounce_task_handle, ...);
}, nullptr);

// 3. Scheduler starts — tasks begin running
vTaskStartScheduler();
```

This looks correct, but there's a subtle problem: **between step 2 and step 3, the scheduler is not running**. If any noise on the pin fires an interrupt in this window, `vTaskNotifyGiveFromISR` sends a notification — but the task is not being scheduled. The notification value gets incremented, `ulTaskNotifyTake` eventually consumes it immediately on first run without blocking, the 50ms delay fires without a real press, `button->get()` returns high (no press), and the event is silently dropped.

More critically, `vTask2` had no delay:

```cpp
static void vTask2(void *pvParameters){
    volatile int b = 0;
    while(1){
        b++;  // no vTaskDelay — runs forever
    }
}
```

This task runs at priority 1, same as `vTask1`. It never yields voluntarily. FreeRTOS will still preempt it for higher-priority tasks (debounce task at priority 2), but it means the system is burning all idle CPU in `vTask2`, which can interfere with tick processing and task switching timing. It's not directly why debounce failed, but it's a latent problem.

---

## 5. Attempt 2: One-Shot Setup Task

To avoid the pre-scheduler race, a dedicated setup task was introduced that arms the interrupt only after the scheduler is running:

```cpp
static void setup_task(void* param){
    (void)param;
    button->setInterruptCallback(GPIO::Edge::Fall, +[](void*) {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        vTaskNotifyGiveFromISR(debounce_task_handle, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }, nullptr);
    vTaskDelete(nullptr);  // self-delete after arming
}

// In start():
xTaskCreate(debounce_task, "debounce", ..., 2, &debounce_task_handle);
xTaskCreate(setup_task,    "setup",    ..., 3, nullptr);  // priority 3 runs first
xTaskCreate(vTask1, "T1", ..., 1, NULL);
xTaskCreate(vTask2, "T2", ..., 1, NULL);
vTaskStartScheduler();
```

`setup_task` at priority 3 runs first when the scheduler starts, arms the interrupt, then calls `vTaskDelete(nullptr)` to delete itself. This required enabling `INCLUDE_vTaskDelete = 1` in `FreeRTOSConfig.h`.

This approach correctly solves the pre-scheduler race — but it still didn't work reliably, because the underlying complexity was the real problem: the debounce task approach has several moving parts that all need to be right simultaneously (task priorities, notification consumption, pin read timing, `vTask2` starvation concerns). Rather than continuing to debug the task machinery for what is essentially a simple problem, a simpler approach was adopted.

---

## 6. Why Attempt 2 Still Didn't Work

Beyond the race condition, the deferred task approach has a fundamental mismatch with this use case:

**The 50ms delay doesn't reset on new bounces.** If the button bounces and fires the ISR at t=0ms, the task wakes, starts the 50ms delay. If another bounce fires at t=10ms, the notification count increments but the task is already in its delay. At t=50ms the task checks the pin — fine. But then `ulTaskNotifyTake` returns immediately (the second notification is pending), the task delays another 50ms, checks again — now 100ms after the original press, the pin is high, the check fails and the event is dropped. For slow mechanical buttons this is fine; for fast or noisy ones it causes missed events.

**The pin check is inherently racy.** `button->get()` reads the pin 50ms after the falling edge. During that 50ms the user could have released and re-pressed the button, leading to incorrect state reads.

---

## 7. The Final Solution: Timestamp Debounce in the ISR

```cpp
button->setInterruptCallback(GPIO::Edge::Fall, +[](void* param) {
    static TickType_t last_tick = 0;
    TickType_t now = xTaskGetTickCountFromISR();
    if ((now - last_tick) > pdMS_TO_TICKS(200)) {
        last_tick = now;
        GPIO* led = static_cast<GPIO*>(param);
        led->toggle();
    }
}, led);
```

### How it works

On every falling edge (button press or bounce), the ISR:

1. Reads the current FreeRTOS tick count
2. Computes how many ticks have passed since the last accepted event
3. If less than 200ms has passed — it's a bounce, ignore it
4. If 200ms or more has passed — it's a genuine press, accept it, update `last_tick`, toggle the LED

Bounces happen within microseconds to low milliseconds of each other. 200ms is far longer than any realistic bounce window, and far shorter than the time between two intentional presses, so the threshold cleanly separates the two.

### Why this works where the task approach didn't

- **No inter-task communication** — nothing to get lost between ISR and task
- **No scheduler dependency** — works before, during, or after scheduler start
- **No pin read** — acts on the edge itself, not on a delayed pin sample
- **Self-contained** — all state (`last_tick`) lives inside the ISR
- **No priority concerns** — no task priority tuning needed

### The subtraction trick for tick overflow

```cpp
if ((now - last_tick) > pdMS_TO_TICKS(200))
```

`TickType_t` is a `uint32_t`. When the tick counter wraps around from `0xFFFFFFFF` to `0x00000000`, the subtraction still gives the correct elapsed time due to unsigned integer wraparound arithmetic. For example:

```
now      = 0x00000005
last_tick = 0xFFFFFFFF
now - last_tick = 0x00000006  ← correct: 6 ticks elapsed
```

This is why you should always use subtraction for elapsed time rather than comparing absolute values.

---

## 8. Deep Dive: `xTaskGetTickCountFromISR`

FreeRTOS has two versions of tick count access:

| Function | Safe to call from |
|----------|------------------|
| `xTaskGetTickCount()` | Tasks only |
| `xTaskGetTickCountFromISR()` | ISRs and tasks |

The `FromISR` suffix is a FreeRTOS convention. Functions with this suffix are interrupt-safe — they don't attempt to take a scheduler lock or disable interrupts in a way that would be illegal from an ISR context. You must always use the `FromISR` variant inside interrupt handlers.

The tick count increments at the rate defined by `configTICK_RATE_HZ` in `FreeRTOSConfig.h`. A common value is 1000 Hz — one tick per millisecond. `pdMS_TO_TICKS(200)` converts 200ms to the equivalent number of ticks, so the code remains correct regardless of what `configTICK_RATE_HZ` is set to.

---

## 9. Deep Dive: Static Local Variables in ISRs

```cpp
+[](void* param) {
    static TickType_t last_tick = 0;  // ← static local
    ...
}
```

A `static` local variable inside a function (or lambda) is:

- **Initialized once** — `last_tick = 0` runs only at program startup, not on every ISR entry
- **Persists between calls** — its value is retained across every invocation of the ISR
- **Stored in `.bss`/`.data`** — not on the stack; it lives in static storage for the lifetime of the program

This is exactly what a debounce timestamp needs. Without `static`, `last_tick` would be a fresh stack variable on every ISR entry, always zero, and the elapsed time check would always see a huge number and always accept every edge — no debounce at all.

One caution: static locals in ISRs are not inherently thread-safe if the ISR can be preempted by a higher-priority ISR that accesses the same variable. In this case `last_tick` is only accessed by this one ISR, so it's safe.

---

## 10. Deep Dive: Captureless Lambdas as Function Pointers

The callback is stored as a plain C function pointer:

```cpp
struct Callback {
    void (*fn)(void*) = nullptr;
    void* ctx = nullptr;
};
```

A C++ lambda with no captures can implicitly convert to a plain function pointer because it has no closure state to carry — it's functionally identical to a free function. The `+` prefix forces this conversion explicitly:

```cpp
+[](void* param) { ... }
// equivalent to writing a named function:
// static void anonymous(void* param) { ... }
```

A lambda **with** captures cannot convert to a function pointer because the captured variables need somewhere to live — they form a closure object. To use capturing lambdas as callbacks you'd need `std::function` (which requires heap allocation and RTTI, both unavailable here) or a different callback design using a template or a fixed-size closure buffer.

The `void* param` context pointer in the `Callback` struct is the bare-metal workaround for this: instead of capturing variables, you pass them through `param`. That's why `led` is passed as the param and cast back inside the lambda.

---

## 11. Pros and Cons of Every Approach

### Raw ISR Toggle (baseline)

```
Pros:
  - Simplest possible code
  - Zero overhead
  - No FreeRTOS dependency
  
Cons:
  - No debounce — bounces cause multiple toggles
  - Unpredictable LED state after a press
```

### Deferred Interrupt Task

```
Pros:
  - Correct FreeRTOS pattern for heavy ISR work
  - ISR stays minimal
  - Can do blocking operations in the task (delays, I2C, UART)
  - Scalable — task can handle complex logic
  
Cons:
  - Many moving parts: task handle, notification, priority tuning
  - Pre-scheduler race condition if interrupt arms before vTaskStartScheduler
  - Notification can be consumed spuriously if fired in wrong window
  - Pin read 50ms later is inherently racy
  - Overkill for a simple toggle
  - Needs vTaskDelete or wastes a task slot for the setup task
```

### Timestamp Debounce in ISR (final solution)

```
Pros:
  - Self-contained — all state inside the ISR
  - No task, no notification, no inter-task communication
  - No scheduler dependency — works at any point
  - Handles burst bounces correctly via the time gate
  - Correct unsigned overflow arithmetic with subtraction
  - ISR remains fast — just two reads and a comparison
  
Cons:
  - Blocks in ISR context (though only for a comparison, not a delay)
  - 200ms threshold is a magic number — too short catches bounces,
    too long makes rapid intentional presses feel sluggish
  - Static state means the debounce is global to the pin —
    if two GPIOs shared the same ISR handler instance, they'd
    share the timestamp (not a problem here since each pin gets
    its own Callback slot)
  - LED is toggled inside the ISR — fine for a GPIO write,
    but would be wrong if the action needed FreeRTOS calls
```

---

## 12. When to Use Which Approach

| Scenario | Recommended approach |
|----------|---------------------|
| Simple toggle, LED, flag | Timestamp debounce in ISR |
| Action requires FreeRTOS blocking call | Deferred interrupt task |
| Action requires I2C/SPI/UART | Deferred interrupt task |
| Multiple buttons with shared logic | Deferred interrupt task with queue |
| Ultra-low latency required | Timestamp in ISR (no task switch overhead) |
| Hardware debounce circuit present | Raw ISR (RC filter + Schmitt trigger handles it) |

The general FreeRTOS guideline is: **keep ISRs as short as possible and defer work to tasks**. The timestamp approach bends this rule slightly by doing the toggle in the ISR, but a single `GPIO::toggle()` is a register write — it takes nanoseconds and is completely safe in ISR context.

If the action ever grows beyond a register write (e.g. "on button press, log to SD card"), migrate to the deferred task pattern at that point.

---

## 13. Summary

| What | Why it failed | Fix |
|------|--------------|-----|
| Raw ISR toggle | No debounce, bounces cause multiple events | — |
| Deferred task, armed before scheduler | Notification lost in pre-scheduler window; pin read racy | Arm after scheduler start |
| Setup task to arm after scheduler | Still complex, `vTask2` starvation, multiple failure modes | Simplify the whole approach |
| Timestamp debounce in ISR | Nothing — this worked | Final solution |

### The working code

```cpp
button->setInterruptCallback(GPIO::Edge::Fall, +[](void* param) {
    static TickType_t last_tick = 0;
    TickType_t now = xTaskGetTickCountFromISR();
    if ((now - last_tick) > pdMS_TO_TICKS(200)) {
        last_tick = now;
        GPIO* led = static_cast<GPIO*>(param);
        led->toggle();
    }
}, led);
```

Five lines. No tasks, no notifications, no race conditions, no priority tuning. The right tool for the job.
