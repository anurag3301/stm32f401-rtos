# GPIO: A Complete Technical Reference
### General Purpose Input/Output — From Fundamentals to Advanced Usage

---

## Table of Contents

1. [Introduction](#1-introduction)
2. [What is a GPIO Pin?](#2-what-is-a-gpio-pin)
3. [GPIO Hardware Architecture](#3-gpio-hardware-architecture)
4. [Pin Electrical Characteristics](#4-pin-electrical-characteristics)
5. [GPIO Modes](#5-gpio-modes)
   - 5.1 [Input Mode](#51-input-mode)
   - 5.2 [Output Mode](#52-output-mode)
   - 5.3 [Alternate Function Mode](#53-alternate-function-mode)
   - 5.4 [Analog Mode](#54-analog-mode)
6. [Pull-Up and Pull-Down Resistors](#6-pull-up-and-pull-down-resistors)
7. [Output Types: Push-Pull vs Open-Drain](#7-output-types-push-pull-vs-open-drain)
8. [Output Speed / Slew Rate](#8-output-speed--slew-rate)
9. [GPIO Registers (STM32F4 CMSIS)](#9-gpio-registers-stm32f4-cmsis)
10. [Configuring GPIO Step by Step](#10-configuring-gpio-step-by-step)
11. [Reading and Writing GPIO](#11-reading-and-writing-gpio)
12. [GPIO Interrupts (EXTI)](#12-gpio-interrupts-exti)
13. [Alternate Functions](#13-alternate-functions)
14. [GPIO in Analog Mode (ADC/DAC)](#14-gpio-in-analog-mode-adcdac)
15. [Common GPIO Patterns and Idioms](#15-common-gpio-patterns-and-idioms)
16. [GPIO Best Practices and Pitfalls](#16-gpio-best-practices-and-pitfalls)
17. [GPIO Across Different Microcontrollers](#17-gpio-across-different-microcontrollers)
18. [Quick Reference Tables](#18-quick-reference-tables)

---

## 1. Introduction

General Purpose Input/Output (GPIO) is the most fundamental building block of any microcontroller peripheral system. Every time a microcontroller blinks an LED, reads a button press, drives a relay, or communicates over SPI — GPIO pins are involved at the lowest hardware level.

Unlike dedicated peripherals (UART, SPI, I2C), which have a fixed purpose wired into silicon, GPIO pins are *programmable*. The same physical pin can act as a digital output driving an LED, a digital input reading a switch, an ADC channel measuring a voltage, or a UART TX line — all depending on how you configure the registers.

This guide covers GPIO from the ground up: the hardware model behind it, how registers control every aspect of pin behaviour, and practical patterns you'll encounter in real embedded systems. Code examples use the STM32F4 series with CMSIS register-level access, but the concepts apply universally.

---

## 2. What is a GPIO Pin?

A GPIO pin is a physical connection point on a microcontroller package (a pad, ball, or through-hole leg) that is connected internally to a configurable digital I/O circuit. From a software perspective it is a single bit — a `1` or a `0` — that can be driven outward onto the physical world or read inward from it.

### The Pin as a Shared Resource

On most modern microcontrollers, GPIO pins are *multiplexed*. A single pin is wired to multiple internal circuits simultaneously. Software selects which circuit is electrically connected to the pad at any given time. These circuits include:

- The GPIO input path (reads voltage on the pad)
- The GPIO output path (drives voltage onto the pad)
- Peripheral A (e.g. UART TX)
- Peripheral B (e.g. SPI SCK)
- The ADC input sampler

The selection mechanism is the **alternate function mux** and the **mode register**, described in detail in later sections.

### GPIO vs. Special-Function Pins

Some pins are *not* GPIO — they are dedicated to specific fixed functions: NRST (reset), BOOT0 (boot mode selection), VDDA (analog power), and oscillator pins OSCIN/OSCOUT. These cannot be repurposed as GPIO without severe system consequences and are treated separately from the GPIO peripheral.

---

## 3. GPIO Hardware Architecture

Understanding what is physically inside the GPIO circuit helps you reason about pull-ups, output drive strength, interrupt detection, and why certain register writes have specific effects.

### Internal Block Diagram

```
                         VDD
                          |
                    [P-MOS FET]  ← Output driver (push-pull high side)
                          |
PAD ──── Input buffer ──── Output node ──── [N-MOS FET]  ← Output driver (low side)
                          |                       |
                    [Schmitt trigger]            GND
                          |
                    [Input data register]
                          |
                    [EXTI / interrupt logic]
```

Key internal components:

**Schmitt Trigger** — conditions the incoming signal. A Schmitt trigger has *hysteresis*: it switches high-to-low at a different voltage threshold than low-to-high. This prevents noisy signals near the threshold from causing rapid oscillation of the input register bit. On STM32F4, the input Schmitt trigger is always active in digital input mode.

**ESD Protection Diodes** — two diodes (one to VDD, one to GND) clamp voltages outside the supply rails. If a pin is driven above VDD or below GND, the diodes conduct and limit damage. However, sustained overvoltage stresses the diodes — always add series resistors when interfacing to higher-voltage signals.

**Output Driver (PMOS/NMOS pair)** — in push-pull mode, both transistors are active: PMOS drives the pin high (to VDD), NMOS drives it low (to GND). In open-drain mode, the PMOS is permanently disabled and only the NMOS pulls low; the pin floats when the NMOS is off.

**Slew Rate Control** — the GPIO peripheral controls how fast the output driver transitions. Slower transitions reduce electromagnetic interference (EMI). Faster transitions are required for high-speed signals.

---

## 4. Pin Electrical Characteristics

Before writing a single line of code, understand the electrical limits of GPIO pins. Violating these limits destroys hardware.

### Voltage Levels (STM32F4, 3.3 V supply)

| Parameter | Symbol | Min | Typical | Max | Unit |
|-----------|--------|-----|---------|-----|------|
| Input high threshold | VIH | 0.65 × VDD | — | — | V |
| Input low threshold | VIL | — | — | 0.35 × VDD | V |
| Output high voltage | VOH | VDD − 0.4 | ≈ 3.0 | — | V |
| Output low voltage | VOL | — | ≈ 0.3 | 0.4 | V |
| Absolute max per pin | VAMAX | −0.3 | — | VDD + 0.3 | V |

> **Critical:** Never apply a voltage higher than VDD + 0.3 V to any GPIO pin. A 5 V signal on a 3.3 V GPIO will damage the pin. Use a level shifter or voltage divider for 5 V interfaces.

### Current Ratings

| Parameter | STM32F4 Value |
|-----------|---------------|
| Max sink/source per pin | 25 mA |
| Max total GPIO current (all ports) | 120 mA |
| Recommended operating current per pin | ≤ 8 mA |

Driving more than 25 mA through a single pin permanently damages the output transistor. To drive loads like motors, relays, or high-power LEDs, use a transistor or MOSFET driven by the GPIO, not the GPIO directly.

### 5 V Tolerance

Some STM32 pins are marked **FT** (five-volt tolerant) in the datasheet. These pins have a modified input path that accepts up to 5 V on the input (even though the output only swings to 3.3 V). Always check the pin table in the datasheet — not all pins are 5 V tolerant.

---

## 5. GPIO Modes

On the STM32F4, each GPIO pin has four possible modes selected by the two-bit `MODER` field for that pin:

| MODER[1:0] | Mode |
|------------|------|
| `00` | Input (reset state for most pins) |
| `01` | General purpose output |
| `10` | Alternate function |
| `11` | Analog |

### 5.1 Input Mode

In input mode the output driver is disconnected. The pad voltage is continuously sampled through the Schmitt trigger and presented in the **IDR** (Input Data Register). The CPU reads this bit to determine whether the external signal is logic-high or logic-low.

**Pull configuration** matters here. Without a pull-up or pull-down resistor, a floating unconnected input will read an indeterminate value — it picks up ambient electromagnetic noise and can oscillate between `0` and `1`.

```c
/* Configure PA0 as digital input with internal pull-up */
GPIOA->MODER  &= ~GPIO_MODER_MODER0;       // 00 = input (clear both bits)
GPIOA->PUPDR  &= ~GPIO_PUPDR_PUPD0;        // clear pull field
GPIOA->PUPDR  |=  GPIO_PUPDR_PUPD0_0;      // 01 = pull-up

/* Read the pin */
uint8_t state = (GPIOA->IDR & GPIO_IDR_ID0) ? 1 : 0;
```

### 5.2 Output Mode

In output mode the output driver is active. Writing a bit to the **ODR** (Output Data Register) or **BSRR** (Bit Set/Reset Register) drives the pad to VDD (logic-high) or GND (logic-low). The input path remains active: you can still read the `IDR` to confirm the actual pin state.

Two sub-properties apply in output mode: output type (push-pull or open-drain) and output speed. Both are discussed in Sections 7 and 8.

```c
/* Configure PA5 as push-pull output, low speed */
GPIOA->MODER  &= ~GPIO_MODER_MODER5;
GPIOA->MODER  |=  GPIO_MODER_MODER5_0;     // 01 = output
GPIOA->OTYPER &= ~GPIO_OTYPER_OT5;         // 0 = push-pull
GPIOA->OSPEEDR&= ~GPIO_OSPEEDR_OSPEED5;    // 00 = low speed
GPIOA->PUPDR  &= ~GPIO_PUPDR_PUPD5;        // 00 = no pull

/* Drive pin high */
GPIOA->BSRR = GPIO_BSRR_BS5;

/* Drive pin low */
GPIOA->BSRR = GPIO_BSRR_BR5;
```

### 5.3 Alternate Function Mode

When a pin is in alternate function mode, the GPIO output driver is controlled by a peripheral (UART, SPI, I2C, TIM, etc.) rather than by the CPU writing to ODR. The pin's actual function is selected by the **AFR** (Alternate Function Register), which maps one of up to 16 peripheral functions onto the pad.

Alternate functions are described in detail in Section 13.

```c
/* PA2 as USART2_TX (AF7 on STM32F4) */
GPIOA->MODER  &= ~GPIO_MODER_MODER2;
GPIOA->MODER  |=  GPIO_MODER_MODER2_1;     // 10 = alternate function
GPIOA->AFR[0] &= ~(0xF << (2 * 4));        // clear AF field for pin 2
GPIOA->AFR[0] |=  (7   << (2 * 4));        // AF7 = USART2
```

### 5.4 Analog Mode

In analog mode both the Schmitt trigger (input) and output driver are disconnected, leaving the pad directly connected to the ADC or DAC peripheral's analog sampler. This eliminates digital switching noise that would otherwise contaminate sensitive analog measurements.

Always configure ADC/DAC pins in analog mode — never leave them in input or output mode when using them for analog signals.

```c
/* PA1 as ADC input (analog mode) */
GPIOA->MODER  &= ~GPIO_MODER_MODER1;
GPIOA->MODER  |=  GPIO_MODER_MODER1_0 | GPIO_MODER_MODER1_1;  // 11 = analog
GPIOA->PUPDR  &= ~GPIO_PUPDR_PUPD1;    // no pull (mandatory for analog pins)
```

---

## 6. Pull-Up and Pull-Down Resistors

When a GPIO pin is in input mode and nothing is driving the pad, the pin floats. A floating input will read random noise — this causes ghost button presses, false interrupt triggers, and unpredictable logic.

Internal pull resistors solve this by weakly biasing the pin to a known state. The `PUPDR` register controls them.

### PUPDR Values

| PUPDR[1:0] | Configuration |
|------------|---------------|
| `00` | No pull (floating) |
| `01` | Pull-up |
| `10` | Pull-down |
| `11` | Reserved |

### Pull-Up Resistor

A pull-up connects the pin to VDD through a resistor (typically 30–50 kΩ on STM32). When nothing drives the pin, it reads `1`. This is the standard configuration for active-low buttons:

```
VDD ──── 40kΩ (internal) ──── Pin ──── [Button] ──── GND
```

When the button opens → pin reads `1` (idle state).  
When the button closes → pin reads `0` (active state).

### Pull-Down Resistor

A pull-down connects the pin to GND through a resistor. When nothing drives the pin, it reads `0`. Used for active-high signals:

```
VDD ──── [Button] ──── Pin ──── 40kΩ (internal) ──── GND
```

When the button opens → pin reads `0` (idle).  
When the button closes → pin reads `1` (active).

### External vs. Internal Pull Resistors

Internal pull resistors are convenient but have high resistance (30–50 kΩ). For noisy environments or long cable runs, add an external pull resistor (1–10 kΩ) that is better matched to the load and less susceptible to induced noise.

For output pins, pull resistors add unnecessary current draw. Disable them (`PUPDR = 00`) on output-configured pins.

---

## 7. Output Types: Push-Pull vs Open-Drain

The `OTYPER` register controls how the output driver behaves when driving the pin. This is one of the most misunderstood GPIO settings.

### 7.1 Push-Pull Output (`OTYPER = 0`)

Both the P-MOS (high-side) and N-MOS (low-side) transistors of the output driver are in use. The driver *actively* drives the pin to VDD when the bit is `1`, and *actively* drives it to GND when the bit is `0`.

```
VDD
 |
PMOS ──── pad (driven HIGH actively)
NMOS ──── pad (driven LOW actively)
 |
GND
```

Use push-pull for standard digital outputs: LEDs, transistor bases, logic-level signals to other chips. It provides the fastest edge rates and the lowest output impedance.

### 7.2 Open-Drain Output (`OTYPER = 1`)

The PMOS high-side driver is permanently disabled. Only the NMOS low-side driver is active. The output can pull the pin *low* but cannot pull it *high*. When the output driver writes `1`, the NMOS turns off and the pin floats — it must be pulled high by an external (or internal) pull-up resistor.

```
VDD ──── pull-up R ──── pad ──── NMOS ──── GND
                         ↑
                    (floats high via R when NMOS off)
```

**Why use open-drain?**

1. **Wired-AND / bus sharing** — multiple devices share a single line (e.g., I2C SDA, I2C SCL). Any device can pull the line low; the line is high only when *all* devices release it. This is impossible with push-pull because two push-pull drivers fighting over the line would short-circuit.

2. **Voltage level shifting** — the pull-up resistor can be tied to a *different* supply voltage than the GPIO. A 3.3 V microcontroller with an open-drain output and a 5 V pull-up can interface natively to 5 V logic.

3. **I2C is always open-drain** — this is mandated by the I2C specification. Configuring I2C pins as push-pull will corrupt the bus and potentially damage devices.

```c
/* PA8 as open-drain output for I2C SCL */
GPIOA->OTYPER |= GPIO_OTYPER_OT8;   // 1 = open-drain
GPIOA->MODER  &= ~GPIO_MODER_MODER8;
GPIOA->MODER  |=  GPIO_MODER_MODER8_0;  // output mode

/* Or let the I2C peripheral manage it via alternate function */
```

---

## 8. Output Speed / Slew Rate

The `OSPEEDR` register controls how fast the output transitions between high and low. On STM32F4:

| OSPEEDR[1:0] | Speed | Typical max frequency |
|--------------|-------|-----------------------|
| `00` | Low | 2 MHz |
| `01` | Medium | 25 MHz |
| `10` | High | 50 MHz |
| `11` | Very high | 100 MHz |

### Why Not Always Use Very High Speed?

Faster edges generate more electromagnetic interference (EMI). A steep voltage edge on a PCB trace radiates like an antenna at harmonics of the switching frequency. For signals below a few MHz — LEDs, buttons, low-speed UART — use low or medium speed to reduce self-interference.

Use high or very high speed only for:
- SPI buses running at tens of MHz
- SDIO / SDMMC interfaces
- USB data lines
- Ethernet RMII/RGMII

```c
/* Set PA5 to medium speed */
GPIOA->OSPEEDR &= ~GPIO_OSPEEDR_OSPEED5;
GPIOA->OSPEEDR |=  GPIO_OSPEEDR_OSPEED5_0;  // 01 = medium
```

> **PCB layout note:** High-speed GPIO outputs benefit from series termination resistors (22–33 Ω) placed close to the driver pin. This reduces ringing and overshoot without significantly affecting edge timing.

---

## 9. GPIO Registers (STM32F4 CMSIS)

Each GPIO port (GPIOA through GPIOK on STM32F429, GPIOA through GPIOI on STM32F407) has its own register block at a fixed base address. All registers are 32-bit wide.

### Register Map

| Register | Offset | Purpose |
|----------|--------|---------|
| `MODER` | 0x00 | Mode: input / output / AF / analog (2 bits per pin) |
| `OTYPER` | 0x04 | Output type: push-pull / open-drain (1 bit per pin) |
| `OSPEEDR` | 0x08 | Output speed (2 bits per pin) |
| `PUPDR` | 0x0C | Pull-up / pull-down (2 bits per pin) |
| `IDR` | 0x10 | Input data register (read-only, 1 bit per pin) |
| `ODR` | 0x14 | Output data register (read/write, 1 bit per pin) |
| `BSRR` | 0x18 | Bit set/reset register (write-only, atomic) |
| `LCKR` | 0x1C | Configuration lock register |
| `AFR[0]` | 0x20 | Alternate function register (pins 0–7, 4 bits each) |
| `AFR[1]` | 0x24 | Alternate function register (pins 8–15, 4 bits each) |

### Clock Enabling

**Before accessing any GPIO register, the peripheral clock must be enabled via RCC.** Accessing GPIO registers without the clock causes a bus fault or returns garbage values.

```c
/* Enable clocks for multiple ports simultaneously */
RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN
              | RCC_AHB1ENR_GPIOBEN
              | RCC_AHB1ENR_GPIOCEN;

/* One read-back to ensure the clock is stable before proceeding */
(void)RCC->AHB1ENR;
```

### MODER Register

```
Bit:  31 30 | 29 28 | ... | 11 10 |  9  8 |  7  6 |  5  4 |  3  2 |  1  0
Pin:   15   |  14   | ... |  5    |  4    |  3    |  2    |  1    |  0
      [1:0] | [1:0] | ... |[1:0]  |[1:0]  |[1:0]  |[1:0]  |[1:0]  |[1:0]
```

Each pin occupies 2 bits. Pin N occupies bits `[2N+1 : 2N]`.

```c
/* Set pin 5 to output (01): clear both bits, then set bit 0 of the pair */
GPIOA->MODER &= ~(0x3U << (5 * 2));  // clear
GPIOA->MODER |=  (0x1U << (5 * 2));  // set to 01
```

### BSRR Register — The Preferred Write Method

`BSRR` is a write-only register. The upper 16 bits are *reset bits* (BR); the lower 16 bits are *set bits* (BS). Writing a `1` to a bit performs the operation atomically — no read-modify-write, no interrupt hazard.

```
Bit:  31 30 ... 17 16 | 15 14 ... 1 0
      BR15 ... BR1 BR0 | BS15 ... BS1 BS0
      ← reset (clear)  | ← set
```

```c
GPIOA->BSRR = (1U << 5);          // set PA5 (same as GPIO_BSRR_BS5)
GPIOA->BSRR = (1U << (5 + 16));   // reset PA5 (same as GPIO_BSRR_BR5)
```

If you write both a set bit and a reset bit for the same pin simultaneously, **the set bit takes priority**.

### ODR Register — Read-Modify-Write

`ODR` supports both reading and writing. Reading it returns the *programmed* output state, not the actual pad voltage (use `IDR` for that). Writes are not atomic: the CPU reads the current value, ORs or ANDs the new bit, and writes back — three operations that can be interrupted.

```c
GPIOA->ODR |=  (1U << 5);   // set PA5   — NOT atomic
GPIOA->ODR &= ~(1U << 5);   // reset PA5 — NOT atomic
GPIOA->ODR ^=  (1U << 5);   // toggle PA5 — NOT atomic
```

> **Rule of thumb:** Use `BSRR` for set and reset in production code. Use `ODR` only for toggle or when you need to write multiple pins simultaneously and atomicity within the port doesn't matter.

### LCKR — Configuration Lock

Writing a specific sequence to `LCKR` permanently locks a pin's configuration registers until the next reset. This prevents accidental reconfiguration of safety-critical pins (e.g., a motor brake output). The lock sequence:

```c
/* Lock PA5: write 1, then 0, then 1 to LCKK (bit 16), keeping the pin mask */
uint32_t mask = GPIO_LCKR_LCK5;
GPIOA->LCKR = GPIO_LCKR_LCKK | mask;  // step 1: LCKK=1
GPIOA->LCKR = mask;                    // step 2: LCKK=0
GPIOA->LCKR = GPIO_LCKR_LCKK | mask;  // step 3: LCKK=1
(void)GPIOA->LCKR;                     // step 4: read back to confirm
```

---

## 10. Configuring GPIO Step by Step

A systematic checklist for configuring any GPIO pin correctly:

### Step 1: Enable the RCC Clock

```c
RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
(void)RCC->AHB1ENR;   // ensure clock is ready
```

### Step 2: Set the Mode (MODER)

Always clear the field first, then write the desired value. Never OR without clearing — the reset value of MODER is not all-zero (some pins default to AF or analog mode).

```c
GPIOA->MODER &= ~GPIO_MODER_MODER5;        // clear (always do this first)
GPIOA->MODER |=  GPIO_MODER_MODER5_0;      // output mode
```

### Step 3: Set the Output Type (OTYPER)

```c
GPIOA->OTYPER &= ~GPIO_OTYPER_OT5;         // push-pull (default)
// OR:
GPIOA->OTYPER |=  GPIO_OTYPER_OT5;         // open-drain
```

### Step 4: Set the Output Speed (OSPEEDR)

```c
GPIOA->OSPEEDR &= ~GPIO_OSPEEDR_OSPEED5;   // clear
GPIOA->OSPEEDR |=  GPIO_OSPEEDR_OSPEED5_0; // medium speed
```

### Step 5: Configure Pull Resistors (PUPDR)

```c
GPIOA->PUPDR &= ~GPIO_PUPDR_PUPD5;         // no pull (appropriate for output)
```

### Step 6: Set Initial State (for outputs)

Always set the initial output state *before* switching to output mode in safety-critical applications to avoid a glitch pulse.

```c
GPIOA->BSRR = GPIO_BSRR_BR5;               // start low before enabling output
/* then set MODER */
```

### Complete Configuration — Helper Macro

```c
/*
 * gpio_configure(port, pin, mode, otype, speed, pull)
 *   mode:  0=input, 1=output, 2=AF, 3=analog
 *   otype: 0=push-pull, 1=open-drain
 *   speed: 0=low, 1=medium, 2=high, 3=very high
 *   pull:  0=none, 1=pull-up, 2=pull-down
 */
static inline void gpio_configure(GPIO_TypeDef *port, uint8_t pin,
                                   uint8_t mode, uint8_t otype,
                                   uint8_t speed, uint8_t pull)
{
    port->MODER   = (port->MODER   & ~(3U << (pin * 2))) | ((uint32_t)mode  << (pin * 2));
    port->OTYPER  = (port->OTYPER  & ~(1U <<  pin))      | ((uint32_t)otype <<  pin);
    port->OSPEEDR = (port->OSPEEDR & ~(3U << (pin * 2))) | ((uint32_t)speed << (pin * 2));
    port->PUPDR   = (port->PUPDR   & ~(3U << (pin * 2))) | ((uint32_t)pull  << (pin * 2));
}

/* Usage: */
gpio_configure(GPIOA, 5, 1, 0, 1, 0);   // PA5: output, push-pull, medium, no pull
```

---

## 11. Reading and Writing GPIO

### Reading a Digital Input

```c
/* Single-pin read — returns 0 or non-zero */
if (GPIOA->IDR & (1U << 0)) {
    /* PA0 is high */
}

/* Returns exactly 0 or 1 */
uint8_t val = (GPIOA->IDR >> 0) & 0x1U;

/* Read the entire port */
uint16_t port_state = (uint16_t)(GPIOA->IDR & 0xFFFF);
```

### Writing a Digital Output

```c
/* Set a pin high (atomic, preferred) */
GPIOA->BSRR = (1U << 5);

/* Set a pin low (atomic, preferred) */
GPIOA->BSRR = (1U << (5 + 16));

/* Toggle a pin (non-atomic — disable IRQ if in shared context) */
GPIOA->ODR ^= (1U << 5);

/* Atomic toggle using BSRR */
if (GPIOA->ODR & (1U << 5))
    GPIOA->BSRR = (1U << (5 + 16));   // currently high → set low
else
    GPIOA->BSRR = (1U << 5);           // currently low → set high

/* Write multiple pins simultaneously */
GPIOA->ODR = (GPIOA->ODR & ~0x00FF) | (new_byte & 0xFF);  // write lower 8 pins
```

### Debouncing a Button Input

Mechanical switches bounce — they rapidly make and break contact for 1–50 ms after a press. Naive reading of `IDR` sees dozens of transitions. Debounce in software:

```c
/* Simple time-based debounce (polling approach) */
#define DEBOUNCE_MS  20

static uint32_t last_stable_time = 0;
static uint8_t  last_stable_state = 1;  // assume pull-up → idle high

uint8_t button_read(void)
{
    uint8_t raw = (GPIOA->IDR & (1U << 0)) ? 1 : 0;
    uint32_t now = get_tick_ms();  // your millisecond tick

    if (raw != last_stable_state) {
        if ((now - last_stable_time) >= DEBOUNCE_MS) {
            last_stable_state = raw;
            last_stable_time  = now;
        }
    } else {
        last_stable_time = now;
    }
    return last_stable_state;
}
```

---

## 12. GPIO Interrupts (EXTI)

The **EXTI** (External Interrupt/Event Controller) allows GPIO pins to trigger CPU interrupts on rising edges, falling edges, or both. This eliminates the need to poll `IDR` in a tight loop.

### EXTI Architecture

Each of the 16 EXTI lines (0–15) corresponds to a pin number (0–15). Any port's pin 5 (PA5, PB5, PC5, …) maps to EXTI line 5. Only **one port** per pin number can be active at a time — you select the port via `SYSCFG_EXTICRx`.

```
PA0 ─┐
PB0 ─┤─→ EXTI line 0 → NVIC IRQ: EXTI0_IRQn
PC0 ─┘   (only one selected at a time via SYSCFG)

PA5 ─┐
PB5 ─┤─→ EXTI line 5 → NVIC IRQ: EXTI9_5_IRQn
PC5 ─┘
```

EXTI lines 0–4 each have a dedicated IRQ. Lines 5–9 share `EXTI9_5_IRQn`. Lines 10–15 share `EXTI15_10_IRQn`.

### Configuring an External Interrupt

```c
#include "stm32f4xx.h"

void exti_button_init(void)
{
    /* 1. Enable clocks */
    RCC->AHB1ENR  |= RCC_AHB1ENR_GPIOAEN;
    RCC->APB2ENR  |= RCC_APB2ENR_SYSCFGEN;  // SYSCFG clock for EXTI mux
    (void)RCC->AHB1ENR;

    /* 2. Configure PA0 as input with pull-up */
    GPIOA->MODER &= ~GPIO_MODER_MODER0;     // input mode
    GPIOA->PUPDR &= ~GPIO_PUPDR_PUPD0;
    GPIOA->PUPDR |=  GPIO_PUPDR_PUPD0_0;    // pull-up

    /* 3. Map EXTI line 0 to port A via SYSCFG */
    SYSCFG->EXTICR[0] &= ~SYSCFG_EXTICR1_EXTI0;  // 0000 = GPIOA
    // (GPIOB = 0001, GPIOC = 0010, etc.)

    /* 4. Configure EXTI trigger — falling edge (button press on pull-up) */
    EXTI->RTSR &= ~(1U << 0);   // disable rising edge
    EXTI->FTSR |=  (1U << 0);   // enable falling edge

    /* 5. Unmask interrupt on EXTI line 0 */
    EXTI->IMR |= (1U << 0);

    /* 6. Enable and set priority in NVIC */
    NVIC_SetPriority(EXTI0_IRQn, 5);
    NVIC_EnableIRQ(EXTI0_IRQn);
}

/* 7. Implement the IRQ handler */
void EXTI0_IRQHandler(void)
{
    if (EXTI->PR & (1U << 0)) {
        EXTI->PR = (1U << 0);   // clear pending flag by writing 1

        /* Your button action here */
        GPIOA->ODR ^= (1U << 5);   // toggle LED
    }
}
```

### Software Interrupt

EXTI can also be triggered in software via the `SWIER` register, useful for testing IRQ handlers without physical hardware:

```c
EXTI->SWIER |= (1U << 0);   // software-trigger EXTI line 0
```

### EXTI Event vs. Interrupt

EXTI has two output paths: interrupt (wakes the CPU) and event (wakes the CPU from WFE sleep without entering an ISR). For ultra-low-power designs, event mode allows the core to resume execution at the instruction after `WFE` without the overhead of an ISR.

```c
EXTI->EMR |= (1U << 0);   // enable event (for WFE wake-up)
EXTI->IMR |= (1U << 0);   // enable interrupt (for ISR)
// Both can be active simultaneously
```

---

## 13. Alternate Functions

In alternate function mode the GPIO pad is connected to an internal peripheral. The `AFR` registers select *which* peripheral from up to 16 options (AF0–AF15).

### AFR Register Layout

`AFR[0]` covers pins 0–7. `AFR[1]` covers pins 8–15. Each pin gets a 4-bit field.

```
AFR[0]:
  Bits [3:0]   → pin 0  AF selection
  Bits [7:4]   → pin 1
  Bits [11:8]  → pin 2
  ...
  Bits [31:28] → pin 7

AFR[1]:
  Bits [3:0]   → pin 8
  ...
  Bits [31:28] → pin 15
```

### Common STM32F4 Alternate Functions

| AF | Peripheral |
|----|-----------|
| AF0 | SYS (MCO, JTAG, SWD) |
| AF1 | TIM1, TIM2 |
| AF2 | TIM3, TIM4, TIM5 |
| AF4 | I2C1, I2C2, I2C3 |
| AF5 | SPI1, SPI2, SPI3, SPI4 |
| AF6 | SPI3, SAI1 |
| AF7 | USART1, USART2, USART3 |
| AF8 | UART4, UART5, USART6 |
| AF9 | CAN1, CAN2, TIM12–14 |
| AF10 | OTG_FS, OTG_HS |
| AF11 | ETH |
| AF12 | FSMC, SDIO, OTG_HS |
| AF15 | EVENTOUT |

> Always verify the AF number for your specific pin in the STM32F4 datasheet Table 9 "Alternate function mapping". Numbers vary by pin.

### Alternate Function Configuration — UART Example

```c
void uart2_gpio_init(void)
{
    /* PA2 = USART2_TX, PA3 = USART2_RX, both AF7 on STM32F4 */
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    (void)RCC->AHB1ENR;

    /* Mode: alternate function (10) */
    GPIOA->MODER &= ~(GPIO_MODER_MODER2 | GPIO_MODER_MODER3);
    GPIOA->MODER |=  (GPIO_MODER_MODER2_1 | GPIO_MODER_MODER3_1);

    /* Output type: push-pull for TX, doesn't matter for RX */
    GPIOA->OTYPER &= ~(GPIO_OTYPER_OT2 | GPIO_OTYPER_OT3);

    /* Speed: high for UART */
    GPIOA->OSPEEDR |= (GPIO_OSPEEDR_OSPEED2 | GPIO_OSPEEDR_OSPEED3);

    /* Pull: pull-up on RX to idle high (UART idle = high) */
    GPIOA->PUPDR &= ~(GPIO_PUPDR_PUPD2 | GPIO_PUPDR_PUPD3);
    GPIOA->PUPDR |=  GPIO_PUPDR_PUPD3_0;  // pull-up on RX only

    /* Alternate function: AF7 for both pins */
    GPIOA->AFR[0] &= ~((0xF << (2 * 4)) | (0xF << (3 * 4)));
    GPIOA->AFR[0] |=  ((7U  << (2 * 4)) | (7U  << (3 * 4)));
}
```

### I2C GPIO Configuration (Open-Drain is Mandatory)

```c
void i2c1_gpio_init(void)
{
    /* PB6 = I2C1_SCL, PB7 = I2C1_SDA, AF4 */
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN;
    (void)RCC->AHB1ENR;

    /* Mode: alternate function */
    GPIOB->MODER &= ~(GPIO_MODER_MODER6 | GPIO_MODER_MODER7);
    GPIOB->MODER |=  (GPIO_MODER_MODER6_1 | GPIO_MODER_MODER7_1);

    /* Output type: OPEN-DRAIN — required by I2C spec */
    GPIOB->OTYPER |= (GPIO_OTYPER_OT6 | GPIO_OTYPER_OT7);

    /* Speed: high */
    GPIOB->OSPEEDR |= (GPIO_OSPEEDR_OSPEED6 | GPIO_OSPEEDR_OSPEED7);

    /* Pull-up: required for I2C (if no external pull-ups fitted) */
    GPIOB->PUPDR &= ~(GPIO_PUPDR_PUPD6 | GPIO_PUPDR_PUPD7);
    GPIOB->PUPDR |=  (GPIO_PUPDR_PUPD6_0 | GPIO_PUPDR_PUPD7_0);

    /* Alternate function: AF4 */
    GPIOB->AFR[0] &= ~((0xF << (6 * 4)) | (0xF << (7 * 4)));
    GPIOB->AFR[0] |=  ((4U  << (6 * 4)) | (4U  << (7 * 4)));
}
```

---

## 14. GPIO in Analog Mode (ADC/DAC)

When a pin is used for analog-to-digital conversion (ADC) or digital-to-analog conversion (DAC), configure it in **analog mode** (`MODER = 11`). This:

- Disconnects the Schmitt trigger input buffer (reduces noise)
- Disconnects the output driver
- Connects the pad directly to the ADC/DAC channel sampler
- Disables internal pull resistors (set PUPDR = 00)

```c
void adc_gpio_init(void)
{
    /* PA1 = ADC1_IN1 */
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    (void)RCC->AHB1ENR;

    /* Analog mode */
    GPIOA->MODER |= (GPIO_MODER_MODER1_0 | GPIO_MODER_MODER1_1);  // 11 = analog

    /* No pull resistors on analog pins */
    GPIOA->PUPDR &= ~GPIO_PUPDR_PUPD1;
}
```

### Analog Pin Cautions

- Never apply voltages above VDDA (the analog supply, typically 3.3 V) to an analog-configured pin.
- Place 100 nF decoupling capacitors as close as possible to VDDA and VREF+ pins.
- Route analog signal traces away from digital signal traces and switching power rails to avoid coupling noise.

---

## 15. Common GPIO Patterns and Idioms

### Pattern 1: Blink an LED

```c
#include "stm32f4xx.h"

static void delay_cycles(volatile uint32_t n) { while (n--); }

int main(void)
{
    /* Clock */
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    (void)RCC->AHB1ENR;

    /* PA5 output, push-pull, low speed */
    GPIOA->MODER  &= ~GPIO_MODER_MODER5;
    GPIOA->MODER  |=  GPIO_MODER_MODER5_0;
    GPIOA->OTYPER &= ~GPIO_OTYPER_OT5;
    GPIOA->OSPEEDR &= ~GPIO_OSPEEDR_OSPEED5;

    while (1) {
        GPIOA->BSRR = GPIO_BSRR_BS5;    // ON
        delay_cycles(1000000);
        GPIOA->BSRR = GPIO_BSRR_BR5;    // OFF
        delay_cycles(1000000);
    }
}
```

### Pattern 2: Read a Button, Drive an LED

```c
/* PA0 = button (pull-up, active-low)
   PA5 = LED (push-pull output)            */

if (!(GPIOA->IDR & GPIO_IDR_ID0))   // button pressed (pin low)
    GPIOA->BSRR = GPIO_BSRR_BS5;    // LED on
else
    GPIOA->BSRR = GPIO_BSRR_BR5;    // LED off
```

### Pattern 3: Write a Byte to an 8-Bit Data Bus

```c
/* Pins PB0–PB7 form an 8-bit parallel data bus */
void bus_write(uint8_t data)
{
    /* Atomically replace only lower 8 bits of ODR */
    GPIOB->ODR = (GPIOB->ODR & 0xFF00U) | data;
}
```

### Pattern 4: Bit-Banged SPI

```c
/* Software SPI using GPIO — works on any MCU */
void spi_transfer(uint8_t byte)
{
    for (int i = 7; i >= 0; i--) {
        /* Data out on falling edge of SCK */
        if (byte & (1U << i))
            GPIOA->BSRR = GPIO_BSRR_BS_MOSI;
        else
            GPIOA->BSRR = GPIO_BSRR_BR_MOSI;

        GPIOA->BSRR = GPIO_BSRR_BS_SCK;   // SCK high
        __NOP(); __NOP();                   // hold time
        GPIOA->BSRR = GPIO_BSRR_BR_SCK;   // SCK low
    }
}
```

### Pattern 5: Driving a MOSFET

```
MCU GPIO (PA6) ──── 100Ω series R ──── Gate
                                         |
                                        NMOS
                                         |
                                        Load
                                         |
                                        GND
                     10kΩ Gate-Source pull-down (ensures off when GPIO floats)
```

```c
/* PA6 high → MOSFET on → load powered */
GPIOA->BSRR = GPIO_BSRR_BS6;  // load ON
GPIOA->BSRR = GPIO_BSRR_BR6;  // load OFF
```

Never connect a GPIO directly to a MOSFET gate without the series resistor — the gate capacitance causes a large instantaneous current that can latch up or destroy the GPIO output driver.

---

## 16. GPIO Best Practices and Pitfalls

### Always Enable the Clock First

Accessing GPIO registers before enabling the RCC clock causes a hard fault (or simply reads/writes nothing). It is the most common beginner mistake.

```c
/* WRONG: */
GPIOA->MODER |= ...;   // hard fault — clock not enabled

/* CORRECT: */
RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
(void)RCC->AHB1ENR;    // settle
GPIOA->MODER |= ...;
```

### Clear Before Setting Multi-Bit Fields

Registers like `MODER`, `OSPEEDR`, and `PUPDR` use multi-bit fields. If you OR a value without clearing the field first, you combine the old bits with the new and get the wrong mode.

```c
/* WRONG (existing bits corrupt the result): */
GPIOA->MODER |= GPIO_MODER_MODER5_0;

/* CORRECT: */
GPIOA->MODER &= ~GPIO_MODER_MODER5;      // clear both bits
GPIOA->MODER |=  GPIO_MODER_MODER5_0;   // then set
```

### Never Use ODR for Atomic Set/Reset in ISR-Mixed Code

If both an ISR and main code write to the same GPIO port via `ODR`, a race condition exists. Use `BSRR` which is a single-cycle atomic write.

### Do Not Exceed Current Limits

The most common hardware damage scenario: driving an LED directly without a current-limiting resistor, or connecting a motor load directly to GPIO. Always calculate:

```
R_series = (V_supply − V_forward_LED) / I_desired
         = (3.3V − 2.0V) / 0.010A
         = 130Ω  → use 150Ω standard value
```

### Check for 5 V Tolerance Before Interfacing

If you connect a 5 V output to a non-5 V-tolerant GPIO input, the ESD diode conducts to the 3.3 V supply, potentially damaging the MCU. Always check the datasheet pin table for the "FT" flag.

### Configure Unused Pins

Floating undriven pins draw current as the input buffer oscillates. Configure all unused pins as analog inputs (lowest power, no toggling) or as low-speed outputs driving a known level.

```c
/* Configure all GPIOC pins as analog to minimize current */
GPIOC->MODER = 0xFFFFFFFF;  // all analog
GPIOC->PUPDR = 0x00000000;  // no pull
```

### Avoid Long GPIO Configuration Functions

Keep GPIO init code modular. One function per peripheral or use case. Mixing peripheral inits into a single monolithic `GPIO_Init()` makes debugging and porting very painful.

---

## 17. GPIO Across Different Microcontrollers

The concepts — mode, pull, output type, speed — are universal. The register names and access patterns differ.

### Arduino (AVR ATmega328P)

AVR GPIO uses three registers per port: `DDRx` (direction), `PORTx` (output/pull), `PINx` (input).

```c
/* PB5 (Arduino pin 13) as output */
DDRB  |=  (1 << 5);   // set bit = output
PORTB |=  (1 << 5);   // high
PORTB &= ~(1 << 5);   // low

/* PB0 as input with pull-up */
DDRB  &= ~(1 << 0);   // clear bit = input
PORTB |=  (1 << 0);   // writing 1 to PORT in input mode = pull-up
uint8_t val = (PINB >> 0) & 0x1;
```

### ESP32

ESP-IDF GPIO API wraps the register interface:

```c
#include "driver/gpio.h"

gpio_config_t cfg = {
    .pin_bit_mask = (1ULL << GPIO_NUM_2),
    .mode         = GPIO_MODE_OUTPUT,
    .pull_up_en   = GPIO_PULLUP_DISABLE,
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .intr_type    = GPIO_INTR_DISABLE,
};
gpio_config(&cfg);
gpio_set_level(GPIO_NUM_2, 1);   // high
gpio_set_level(GPIO_NUM_2, 0);   // low
```

### Raspberry Pi (Linux `libgpiod`)

On Linux, GPIO is accessed through the kernel `libgpiod` interface (replaces the deprecated `/sys/class/gpio`):

```c
#include <gpiod.h>

struct gpiod_chip *chip = gpiod_chip_open("/dev/gpiochip0");
struct gpiod_line *line = gpiod_chip_get_line(chip, 17);  // GPIO17

gpiod_line_request_output(line, "my-app", 0);  // output, initial value 0
gpiod_line_set_value(line, 1);                 // high
gpiod_line_set_value(line, 0);                 // low
gpiod_line_release(line);
gpiod_chip_close(chip);
```

### nRF52 (Nordic)

```c
#include "nrf_gpio.h"

nrf_gpio_cfg_output(LED_PIN);
nrf_gpio_pin_set(LED_PIN);    // high
nrf_gpio_pin_clear(LED_PIN);  // low
nrf_gpio_pin_toggle(LED_PIN); // toggle
```

---

## 18. Quick Reference Tables

### STM32F4 GPIO Port Base Addresses

| Port | Base Address |
|------|-------------|
| GPIOA | 0x40020000 |
| GPIOB | 0x40020400 |
| GPIOC | 0x40020800 |
| GPIOD | 0x40020C00 |
| GPIOE | 0x40021000 |
| GPIOF | 0x40021400 |
| GPIOG | 0x40021800 |
| GPIOH | 0x40021C00 |
| GPIOI | 0x40022000 |

### MODER Values

| Value | Mode |
|-------|------|
| `00` | Input |
| `01` | General purpose output |
| `10` | Alternate function |
| `11` | Analog |

### OTYPER Values

| Value | Type |
|-------|------|
| `0` | Push-pull |
| `1` | Open-drain |

### OSPEEDR Values

| Value | Speed | Max Freq |
|-------|-------|---------|
| `00` | Low | 2 MHz |
| `01` | Medium | 25 MHz |
| `10` | High | 50 MHz |
| `11` | Very high | 100 MHz |

### PUPDR Values

| Value | Configuration |
|-------|--------------|
| `00` | No pull |
| `01` | Pull-up |
| `10` | Pull-down |
| `11` | Reserved |

### EXTI IRQ Mapping

| EXTI Lines | IRQ Handler |
|------------|-------------|
| 0 | `EXTI0_IRQHandler` |
| 1 | `EXTI1_IRQHandler` |
| 2 | `EXTI2_IRQHandler` |
| 3 | `EXTI3_IRQHandler` |
| 4 | `EXTI4_IRQHandler` |
| 5–9 | `EXTI9_5_IRQHandler` |
| 10–15 | `EXTI15_10_IRQHandler` |

### LED Current Limiting Resistor (3.3 V supply)

| LED colour | Forward voltage | Resistor (10 mA) | Resistor (5 mA) |
|-----------|----------------|-----------------|-----------------|
| Red | 1.8–2.2 V | 110–150 Ω | 220–300 Ω |
| Green | 2.0–2.4 V | 90–130 Ω | 180–260 Ω |
| Blue | 3.0–3.5 V | 0–30 Ω | 0–60 Ω |
| White | 3.0–3.5 V | 0–30 Ω | 0–60 Ω |
| Yellow | 1.8–2.2 V | 110–150 Ω | 220–300 Ω |

> For blue/white LEDs with a forward voltage close to VDD, limit current carefully — the resistor is near-zero and any supply variation changes the current significantly. Use a constant-current driver for professional designs.

---

*This document covers GPIO fundamentals and STM32F4-specific implementation. Always refer to the official STM32F4xx Reference Manual (RM0090) and your device's datasheet for authoritative register definitions and pin assignments.*
