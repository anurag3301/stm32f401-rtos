# RCC & Clock System: A Complete Technical Reference
### Reset and Clock Control — From Oscillators to Peripheral Buses on STM32F4

---

## Table of Contents

1. [Introduction](#1-introduction)
2. [Why Clocks Matter](#2-why-clocks-matter)
3. [Clock Sources Overview](#3-clock-sources-overview)
4. [HSI — High-Speed Internal Oscillator](#4-hsi--high-speed-internal-oscillator)
5. [HSE — High-Speed External Oscillator](#5-hse--high-speed-external-oscillator)
6. [LSI — Low-Speed Internal Oscillator](#6-lsi--low-speed-internal-oscillator)
7. [LSE — Low-Speed External Oscillator](#7-lse--low-speed-external-oscillator)
8. [PLL — Phase-Locked Loop](#8-pll--phase-locked-loop)
9. [System Clock (SYSCLK) Selection](#9-system-clock-sysclk-selection)
10. [AHB, APB1, and APB2 Bus Clocks](#10-ahb-apb1-and-apb2-bus-clocks)
11. [Flash Latency and the ACR Register](#11-flash-latency-and-the-acr-register)
12. [RCC Registers In Depth](#12-rcc-registers-in-depth)
13. [Step-by-Step: Full 168 MHz PLL Setup](#13-step-by-step-full-168-mhz-pll-setup)
14. [Peripheral Clock Enable and Reset](#14-peripheral-clock-enable-and-reset)
15. [Clock Security System (CSS)](#15-clock-security-system-css)
16. [MCO — Master Clock Output](#16-mco--master-clock-output)
17. [RTC Clock Domain](#17-rtc-clock-domain)
18. [Low-Power Clock Considerations](#18-low-power-clock-considerations)
19. [Clock Measurement and Calibration](#19-clock-measurement-and-calibration)
20. [Common Mistakes and Pitfalls](#20-common-mistakes-and-pitfalls)
21. [Quick Reference Tables](#21-quick-reference-tables)

---

## 1. Introduction

Every digital circuit needs a clock — a periodic signal that tells all logic when to latch data, increment counters, and advance state machines. In a microcontroller like the STM32F4, there is not one clock but a *system* of clocks: multiple oscillators, a phase-locked loop (PLL) that multiplies frequencies, a hierarchy of bus dividers, and independent clock domains for the CPU, high-speed peripherals, low-speed peripherals, the real-time clock, and the USB engine.

The **Reset and Clock Control (RCC)** peripheral is the master controller of all of this. It decides which oscillator is active, what the PLL multiplies the frequency to, how fast each bus runs, and which peripheral is allowed to receive a clock at all. Getting the RCC configuration right is the very first thing any embedded program must do — before UART, before ADC, before GPIO outputs mean anything.

This guide covers everything: the hardware model of each clock source, the mathematics of PLL configuration, the relationship between bus speeds and peripheral timers, flash wait states, and the code patterns that make clock setup reliable and portable.

---

## 2. Why Clocks Matter

### Timing Correctness

Baud rates, PWM frequencies, ADC sampling rates, I2C/SPI speeds — all of these are derived from the peripheral clock. If you configure UART for 115200 baud but the peripheral clock is running at half the expected frequency, the actual baud rate will be 57600. The UART will appear to work but corrupt every byte.

### Power Consumption

Clock frequency is the dominant factor in dynamic power consumption. A Cortex-M4 running at 168 MHz consumes roughly 10× more dynamic power than the same code running at 16 MHz. Peripherals that are clocked but idle still consume leakage current proportional to the clock speed. Careful clock management — enabling clocks only when needed, reducing speeds in non-critical phases — can reduce system power by orders of magnitude.

### Electromagnetic Interference

Every clock edge causes current to flow in supply and ground planes. The faster the clock, the higher the frequency content of the radiated emissions. Systems with regulatory EMI requirements (CE, FCC) must balance performance against radiated emissions — running peripherals only as fast as necessary is standard practice.

### Peripheral Interdependencies

Some peripherals share clock domains or have constraints relative to each other. For example:
- The APB timer clocks are doubled when the APB prescaler is not 1 (TIM clock = 2 × APB clock if APB ≠ AHB).
- The USB OTG FS peripheral requires exactly 48 MHz from the PLLQ output — no deviation tolerated.
- The Ethernet MAC requires a 25 MHz or 50 MHz reference.
- The SDIO peripheral requires a clock derived from SYSCLK within a specific range.

Understanding the full clock tree prevents hours of debugging subtle peripheral failures.

---

## 3. Clock Sources Overview

The STM32F4 has four independent oscillators plus a PLL that can be driven by two of them.

```
┌─────────────────────────────────────────────────────────────────┐
│                        STM32F4 Clock Tree                       │
│                                                                 │
│  HSI (16 MHz RC) ──┬──────────────────────────→ SYSCLK mux     │
│                    └──→ PLL input mux ──→ PLL ──→ PLLP ──→ SYSCLK mux │
│                                               ├──→ PLLQ ──→ USB/SDIO   │
│  HSE (4–26 MHz) ───┬──────────────────────────→ SYSCLK mux     │
│                    └──→ PLL input mux                           │
│                                                                 │
│  SYSCLK ──→ AHB prescaler ──→ HCLK (CPU, DMA, AHB peripherals) │
│                              ├──→ APB1 prescaler ──→ PCLK1     │
│                              └──→ APB2 prescaler ──→ PCLK2     │
│                                                                 │
│  LSI (32 kHz RC) ─────────────────────────────→ RTC / IWDG     │
│  LSE (32.768 kHz XTAL) ───────────────────────→ RTC            │
└─────────────────────────────────────────────────────────────────┘
```

| Source | Frequency | Type | Use |
|--------|-----------|------|-----|
| HSI | 16 MHz | RC (internal) | Default boot clock, PLL input |
| HSE | 4–26 MHz | Crystal / oscillator (external) | Accurate PLL input |
| LSI | ~32 kHz | RC (internal) | IWDG, RTC (inaccurate) |
| LSE | 32.768 kHz | Crystal (external) | RTC (accurate) |
| PLL | Up to 168 MHz (PLLP) | Derived | SYSCLK, USB, SDIO |

---

## 4. HSI — High-Speed Internal Oscillator

The HSI is a 16 MHz RC (resistor-capacitor) oscillator built directly into the STM32F4 silicon. It requires no external components and is available immediately after reset — no startup delay.

### Characteristics

| Parameter | Value |
|-----------|-------|
| Nominal frequency | 16 MHz |
| Accuracy (25 °C, calibrated) | ±1% |
| Accuracy (full temp range) | ±2–3% |
| Startup time | ~2 μs |
| Current consumption | ~100 μA |

### HSI at Reset

The STM32F4 boots from HSI by default. `SYSCLK = HSI = 16 MHz` at power-on. The first job of your clock init code is to decide whether to stay on HSI or switch to a higher-frequency source.

### HSI Trimming and Calibration

The HSI frequency can be trimmed via the `HSITRIM` field in `RCC_CR`. Each step shifts the frequency by approximately 40 kHz. The factory calibration value (loaded from device-specific memory at reset) is in `HSICAL`. Do not normally write `HSICAL` — it is read-only and set by the factory. Adjust `HSITRIM` if your application needs better accuracy than the factory calibration provides.

```c
/* Read factory calibration */
uint8_t hsical = (RCC->CR & RCC_CR_HSICAL) >> RCC_CR_HSICAL_Pos;

/* Adjust trim: add +2 steps from factory default */
uint8_t current_trim = (RCC->CR & RCC_CR_HSITRIM) >> RCC_CR_HSITRIM_Pos;
RCC->CR = (RCC->CR & ~RCC_CR_HSITRIM) |
          ((current_trim + 2) << RCC_CR_HSITRIM_Pos);
```

### When to Use HSI

- In low-power modes where crystal startup time would add too much latency
- During initial startup before the HSE/PLL settles
- As a fallback when the CSS (Clock Security System) detects HSE failure
- In cost-sensitive designs where a crystal is not fitted

### HSI Limitation

The HSI is unsuitable for USB (requires exactly 48 MHz ±0.25%), precise baud rates over long periods, or any application where ±2% frequency error causes protocol failures.

---

## 5. HSE — High-Speed External Oscillator

The HSE uses an external crystal or oscillator to generate a precise clock reference. On the Nucleo-F4 development board, an 8 MHz crystal is fitted. Custom boards may use 4–26 MHz crystals.

### HSE Operating Modes

**Crystal/Ceramic Resonator Mode** — The STM32 drives the crystal through its internal oscillator circuit (OSC_IN, OSC_OUT pins). The crystal frequency must be between 4 and 26 MHz.

```
OSC_IN  ──── Crystal ──── OSC_OUT
         ├── load cap ──┤
        GND             GND
         (typically 12–22 pF each, check crystal datasheet)
```

**External Clock (Bypass) Mode** — A pre-existing clock signal (e.g., from a clock generator IC) is fed directly into OSC_IN. OSC_OUT is unused. Enable bypass mode with `HSEBYP` before enabling `HSEON`.

```c
/* HSE in bypass mode (external clock generator on OSC_IN) */
RCC->CR |= RCC_CR_HSEBYP;   // must set BEFORE HSEON
RCC->CR |= RCC_CR_HSEON;
while (!(RCC->CR & RCC_CR_HSERDY));
```

### HSE Startup Sequence

```c
/* 1. Enable HSE oscillator */
RCC->CR |= RCC_CR_HSEON;

/* 2. Wait for HSE ready flag */
uint32_t timeout = 100000;
while (!(RCC->CR & RCC_CR_HSERDY) && --timeout);

if (!timeout) {
    /* HSE failed to start — handle error */
    /* Fall back to HSI or enter safe mode */
    Error_Handler();
}
/* HSE is now stable and ready */
```

### HSE Ready Time

A crystal oscillator takes time to build up amplitude and stabilise. Typical startup time is 1–5 ms for standard AT-cut 8 MHz crystals. If you poll `HSERDY` and the flag never sets, check:
- Crystal is correctly fitted (not DNP)
- Load capacitors match the crystal's specified load capacitance
- No short circuits on OSC_IN/OSC_OUT
- No stray capacitance from long PCB traces near OSC_IN

### Crystal Selection Guidelines

| Parameter | Recommendation |
|-----------|----------------|
| Frequency | 8 MHz typical (matches Nucleo, simplifies PLL) |
| Load capacitance (CL) | 12–18 pF (match with PCB caps) |
| ESR | < 100 Ω (lower ESR = faster startup) |
| Temperature stability | ±30 ppm standard, ±10 ppm for communications |
| PCB trace length | < 5 mm from pins to crystal |

---

## 6. LSI — Low-Speed Internal Oscillator

The LSI is an ~32 kHz RC oscillator used primarily by the **Independent Watchdog (IWDG)** and optionally by the **RTC**. It requires no external components.

### Characteristics

| Parameter | Value |
|-----------|-------|
| Nominal frequency | 32 kHz |
| Actual range | 17–47 kHz |
| Accuracy | ±15% over temperature and voltage |
| Current consumption | ~1 μA |

The LSI's ±15% accuracy makes it unsuitable for anything requiring precise timing. Use it for the IWDG (which only needs a rough period) or as an RTC source when crystal cost must be minimised and ±15% drift is acceptable.

```c
/* Enable LSI */
RCC->CSR |= RCC_CSR_LSION;
while (!(RCC->CSR & RCC_CSR_LSIRDY));
```

### Measuring LSI Frequency

TIM5 channel 4 can be internally connected to LSI for frequency measurement. This lets you determine the actual LSI frequency on your specific device at runtime:

```c
/* Remap TIM5 CH4 to LSI (internal connection) */
RCC->APB1ENR |= RCC_APB1ENR_TIM5EN;
TIM5->OR |= TIM5_OR_TI4_RMP_0 | TIM5_OR_TI4_RMP_1; // remap CH4 to LSI

/* Capture two rising edges, compute period in TIM5 counts */
/* LSI_freq = TIM5_freq / (capture2 - capture1) */
```

---

## 7. LSE — Low-Speed External Oscillator

The LSE uses a 32.768 kHz watch crystal connected to PC14 (OSC32_IN) and PC15 (OSC32_OUT). It is the standard clock source for the **Real-Time Clock (RTC)** because 32768 = 2¹⁵, making power-of-two prescaling to 1 Hz trivial.

### Characteristics

| Parameter | Value |
|-----------|-------|
| Frequency | 32.768 kHz |
| Accuracy | ±20–100 ppm (crystal-dependent) |
| Startup time | 0.5–2 seconds (slow — plan accordingly) |
| Current | ~1 μA |

### LSE Startup

The LSE lives in the **backup power domain** (VBAT). Accessing backup domain registers requires:
1. Enabling the PWR peripheral clock
2. Disabling the backup domain write protection

```c
/* Enable PWR clock */
RCC->APB1ENR |= RCC_APB1ENR_PWREN;

/* Disable backup domain write protection */
PWR->CR |= PWR_CR_DBP;

/* Enable LSE oscillator */
RCC->BDCR |= RCC_BDCR_LSEON;

/* Wait for LSE ready (can take up to 2 seconds!) */
uint32_t timeout = 5000000;
while (!(RCC->BDCR & RCC_BDCR_LSERDY) && --timeout);
```

### LSE Drive Strength

On some STM32F4 devices and PCB layouts, the LSE fails to start because the crystal's ESR is too high or PCB capacitance is too large. The `LSEDRV` bits in `RCC_BDCR` increase the oscillator drive current:

| LSEDRV | Drive | Use when |
|--------|-------|---------|
| `00` | Low (default) | Standard crystals, short traces |
| `01` | Medium-low | Moderate load |
| `10` | Medium-high | High-ESR crystals |
| `11` | High | Very high load, long traces |

```c
/* Set high drive before enabling LSE */
RCC->BDCR = (RCC->BDCR & ~RCC_BDCR_LSEDRV) | RCC_BDCR_LSEDRV_1;
RCC->BDCR |= RCC_BDCR_LSEON;
```

> Increase drive gradually and only as needed — high drive consumes more current and increases crystal stress.

### LSE Crystal PCB Guidelines

- Place 12.5 pF load capacitors (or as specified by crystal datasheet) directly at PC14/PC15
- Keep traces under 5 mm, symmetric, shielded from aggressor signals
- Place a ground guard ring around the crystal
- Avoid placing vias under or near the crystal

---

## 8. PLL — Phase-Locked Loop

The PLL is the key component that takes a modest oscillator frequency (16 MHz HSI or 4–26 MHz HSE) and multiplies it to the high frequencies required for full-performance operation (up to 168 MHz on STM32F407, 180 MHz on STM32F429).

### PLL Architecture

The STM32F4 main PLL has three configurable parameters and three outputs:

```
                    ┌─────────────────────────────────────┐
                    │              Main PLL                │
                    │                                      │
PLL input ──→ ÷ PLLM ──→ VCO input ──→ × PLLN ──→ VCO output │
(HSI or HSE)   (2–63)   (1–2 MHz target) (50–432 MHz) (100–432 MHz) │
                    │        ├──→ ÷ PLLP ──→ PLLCLK (SYSCLK) │
                    │        ├──→ ÷ PLLQ ──→ USB / SDIO / RNG │
                    │        └──→ ÷ PLLR ──→ I2S / SAI (F42x) │
                    └─────────────────────────────────────┘
```

### PLL Parameters

**PLLM (division factor, 2–63):**  
Divides the PLL input clock to produce the VCO input frequency. The VCO input must be between **1 and 2 MHz**. Target 1 MHz for lowest jitter; 2 MHz for fastest lock time.

```
VCO_input = PLL_source / PLLM
Constraint: 1 MHz ≤ VCO_input ≤ 2 MHz
```

**PLLN (multiplication factor, 50–432):**  
Multiplies the VCO input to produce the VCO output frequency.

```
VCO_output = VCO_input × PLLN
Constraint: 100 MHz ≤ VCO_output ≤ 432 MHz
```

**PLLP (post-divider, 2/4/6/8):**  
Divides the VCO output to produce PLLCLK, which feeds SYSCLK.

```
PLLCLK = VCO_output / PLLP
Constraint: PLLCLK ≤ 168 MHz (F407) or 180 MHz (F429)
```

**PLLQ (division factor, 2–15):**  
Divides the VCO output to produce the USB/SDIO/RNG clock. USB requires exactly **48 MHz**.

```
PLLQ_CLK = VCO_output / PLLQ
USB constraint: PLLQ_CLK = 48 MHz exactly
```

### PLL Calculation — 168 MHz from 8 MHz HSE

This is the most common STM32F4 configuration (Nucleo boards, many custom designs):

```
Goal: SYSCLK = 168 MHz, USB = 48 MHz, HSE = 8 MHz

Step 1: VCO input
  PLLM = 8   →   VCO_input = 8 MHz / 8 = 1 MHz  ✓ (in range)

Step 2: VCO output
  PLLN = 336  →  VCO_output = 1 MHz × 336 = 336 MHz  ✓ (in range 100–432)

Step 3: SYSCLK
  PLLP = 2   →   PLLCLK = 336 MHz / 2 = 168 MHz  ✓

Step 4: USB clock
  PLLQ = 7   →   USB_CLK = 336 MHz / 7 = 48 MHz  ✓

Register values: PLLM=8, PLLN=336, PLLP=0 (00=÷2), PLLQ=7
```

### PLL Calculation — 168 MHz from 16 MHz HSI

```
Goal: SYSCLK = 168 MHz, USB = 48 MHz, HSI = 16 MHz

Step 1: VCO input
  PLLM = 16  →   VCO_input = 16 MHz / 16 = 1 MHz  ✓

Step 2: VCO output
  PLLN = 336  →  VCO_output = 1 MHz × 336 = 336 MHz  ✓

Step 3: SYSCLK
  PLLP = 2   →   PLLCLK = 336 / 2 = 168 MHz  ✓

Step 4: USB clock
  PLLQ = 7   →   USB_CLK = 336 / 7 = 48 MHz  ✓

Register values: PLLM=16, PLLN=336, PLLP=0, PLLQ=7
```

### PLL Calculation — 180 MHz from 8 MHz HSE (STM32F429)

```
Goal: SYSCLK = 180 MHz, USB = 45 MHz (NOT 48 — F429 has separate USB PLL)

  PLLM = 8
  VCO_input = 8 / 8 = 1 MHz
  PLLN = 360
  VCO_output = 1 × 360 = 360 MHz
  PLLP = 2  →  SYSCLK = 360 / 2 = 180 MHz
  PLLQ = 8  →  PLLQ_CLK = 360 / 8 = 45 MHz (F429 uses SAI PLL for USB)
```

### PLL Constraint Checklist

Before finalising PLL values, verify all constraints:

```
□  1 MHz  ≤ VCO_input  ≤ 2 MHz
□  100 MHz ≤ VCO_output ≤ 432 MHz
□  PLLP ∈ {2, 4, 6, 8}  (not arbitrary division)
□  PLLCLK ≤ 168 MHz (F407) or 180 MHz (F429)
□  PLLQ_CLK = 48 MHz if USB is used
□  PLLM ∈ [2, 63]
□  PLLN ∈ [50, 432]
□  PLLQ ∈ [2, 15]
```

### PLL Register Encoding

```c
/* PLLCFGR register layout:
   Bits [5:0]   = PLLM  (2–63)
   Bits [14:6]  = PLLN  (50–432)
   Bits [17:16] = PLLP  (00=÷2, 01=÷4, 10=÷6, 11=÷8)
   Bit  [22]    = PLLSRC (0=HSI, 1=HSE)
   Bits [27:24] = PLLQ  (2–15)
*/

#define PLL_M   8
#define PLL_N   336
#define PLL_P   0    /* 0 = ÷2, 1 = ÷4, 2 = ÷6, 3 = ÷8 */
#define PLL_Q   7

RCC->PLLCFGR = (PLL_M)
             | (PLL_N << 6)
             | (PLL_P << 16)
             | RCC_PLLCFGR_PLLSRC_HSE   // use HSE as PLL input
             | (PLL_Q << 24);
```

### Enabling the PLL

The PLL must be configured while it is disabled. Once enabled, its parameters cannot be changed without disabling it first.

```c
/* 1. Ensure PLL is off (it should be at reset) */
RCC->CR &= ~RCC_CR_PLLON;

/* 2. Write configuration */
RCC->PLLCFGR = /* ... your values ... */;

/* 3. Enable PLL */
RCC->CR |= RCC_CR_PLLON;

/* 4. Wait for PLL lock */
while (!(RCC->CR & RCC_CR_PLLRDY));

/* PLL is now locked and stable */
```

### PLL Lock Time

The PLL requires a settling time to lock after being enabled. The `PLLRDY` flag indicates lock. Typical lock time is 100–200 μs. Always poll `PLLRDY` — never assume the PLL is locked immediately after enabling it.

---

## 9. System Clock (SYSCLK) Selection

SYSCLK is the root clock for the entire CPU and bus system. The `SW` (System Clock Switch) field in `RCC_CFGR` selects the source:

| SW[1:0] | SYSCLK source |
|---------|--------------|
| `00` | HSI (16 MHz, default at reset) |
| `01` | HSE |
| `10` | PLLCLK |
| `11` | Reserved |

The `SWS` (System Clock Switch Status) field confirms which source is actually active. Always read `SWS` after writing `SW` to confirm the switch completed before assuming the new clock is active.

```c
/* Switch SYSCLK to PLL */
RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_PLL;

/* Wait until the switch is confirmed */
while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL);

/* SYSCLK is now sourced from PLL */
```

### Clock Switch Safety Rules

1. **Flash latency must be set BEFORE increasing SYSCLK** — if you switch to 168 MHz before setting the correct flash wait states, the CPU will fetch garbage instructions and crash. See Section 11.
2. **Voltage scaling must be correct** — the STM32F4 has a power regulator with multiple voltage modes. At 168 MHz, the regulator must be in Scale 1 (highest voltage). See Section 11.
3. **Never disable the current clock source before confirming the new source is active** — the switch is a hardware process that takes a few cycles. The `SWS` field is your proof.

---

## 10. AHB, APB1, and APB2 Bus Clocks

SYSCLK feeds a tree of prescalers that produce the clocks for the CPU and each peripheral bus. Understanding this tree is essential for correct peripheral clock calculations.

### The Bus Hierarchy

```
SYSCLK (168 MHz)
    │
    ├──→ AHB prescaler (÷1) ──→ HCLK (168 MHz)
    │        │
    │        ├──→ CPU (Cortex-M4) core clock
    │        ├──→ DMA1, DMA2
    │        ├──→ AHB peripherals (Ethernet, USB OTG HS, CRC, FSMC)
    │        ├──→ SysTick timer (HCLK or HCLK/8)
    │        │
    │        ├──→ APB1 prescaler (÷4) ──→ PCLK1 (42 MHz)
    │        │        │
    │        │        ├──→ APB1 peripherals (I2C, SPI2/3, USART2–5, DAC, CAN, TIM2–7, TIM12–14)
    │        │        └──→ APB1 timers ──→ TIM clock = 2 × PCLK1 = 84 MHz (because APB1 ≠ HCLK)
    │        │
    │        └──→ APB2 prescaler (÷2) ──→ PCLK2 (84 MHz)
    │                 │
    │                 ├──→ APB2 peripherals (USART1/6, SPI1/4/5/6, ADC1–3, SDIO, TIM1, TIM8–11)
    │                 └──→ APB2 timers ──→ TIM clock = 2 × PCLK2 = 168 MHz (because APB2 ≠ HCLK)
    │
    └──→ PLLQ ──→ USB OTG FS (48 MHz), SDIO, RNG
```

### Maximum Bus Frequencies (STM32F407 at 168 MHz)

| Clock | Max Frequency | Prescaler (at 168 MHz) |
|-------|--------------|----------------------|
| HCLK (AHB) | 168 MHz | ÷1 |
| PCLK1 (APB1) | 42 MHz | ÷4 |
| PCLK2 (APB2) | 84 MHz | ÷2 |

### The Timer Clock Doubling Rule

This is one of the most misunderstood aspects of the STM32 clock tree. When an APB prescaler divides by anything other than 1, the timers on that bus receive **twice** the APB clock:

```
If APBx prescaler = 1:  TIMx_CLK = PCLKx
If APBx prescaler ≠ 1:  TIMx_CLK = 2 × PCLKx
```

At the standard 168 MHz configuration:
- APB1 prescaler = 4 → PCLK1 = 42 MHz → TIM2–7 clock = **84 MHz**
- APB2 prescaler = 2 → PCLK2 = 84 MHz → TIM1, TIM8 clock = **168 MHz**

If you calculate a TIM prescaler based on PCLK1 (42 MHz) instead of the actual timer input clock (84 MHz), your PWM frequency will be half what you expect.

### HPRE, PPRE1, PPRE2 — Prescaler Register Values

These fields live in `RCC_CFGR`:

| Field | Bits | Controls |
|-------|------|---------|
| `HPRE[3:0]` | [7:4] | AHB prescaler (SYSCLK → HCLK) |
| `PPRE1[2:0]` | [12:10] | APB1 prescaler (HCLK → PCLK1) |
| `PPRE2[2:0]` | [15:13] | APB2 prescaler (HCLK → PCLK2) |

**HPRE values:**

| HPRE | Division |
|------|---------|
| `0xxx` | ÷1 (any value with MSB=0) |
| `1000` | ÷2 |
| `1001` | ÷4 |
| `1010` | ÷8 |
| `1011` | ÷16 |
| `1100` | ÷64 |
| `1101` | ÷128 |
| `1110` | ÷256 |
| `1111` | ÷512 |

**PPRE1 / PPRE2 values:**

| PPRE | Division |
|------|---------|
| `0xx` | ÷1 |
| `100` | ÷2 |
| `101` | ÷4 |
| `110` | ÷8 |
| `111` | ÷16 |

```c
/* Configure bus prescalers for 168 MHz operation:
   AHB = ÷1 = 168 MHz, APB1 = ÷4 = 42 MHz, APB2 = ÷2 = 84 MHz */
RCC->CFGR |= RCC_CFGR_HPRE_DIV1     // AHB: no division
           | RCC_CFGR_PPRE1_DIV4    // APB1: ÷4
           | RCC_CFGR_PPRE2_DIV2;   // APB2: ÷2
```

---

## 11. Flash Latency and the ACR Register

This section is critical. The flash memory on the STM32F4 cannot be read instantaneously — it requires a fixed number of CPU clock cycles per access (called *wait states* or *latency*). If you run the CPU faster than the flash can respond, the CPU fetches garbage data and crashes in undefined ways.

**The flash latency must be configured before increasing the clock frequency.**

### Flash Latency Table (3.3 V supply, STM32F407)

| SYSCLK range | Required wait states | LATENCY field |
|-------------|---------------------|---------------|
| 0 – 30 MHz | 0 WS | `0b000` |
| 30 – 60 MHz | 1 WS | `0b001` |
| 60 – 90 MHz | 2 WS | `0b010` |
| 90 – 120 MHz | 3 WS | `0b011` |
| 120 – 150 MHz | 4 WS | `0b100` |
| 150 – 168 MHz | 5 WS | `0b101` |

At 168 MHz: use **5 wait states**.

### Flash ACR Register

The `FLASH_ACR` register controls flash access:

| Field | Bits | Purpose |
|-------|------|---------|
| `LATENCY[2:0]` | [2:0] | Wait states |
| `PRFTEN` | [8] | Prefetch buffer enable |
| `ICEN` | [9] | Instruction cache enable |
| `DCEN` | [10] | Data cache enable |

### Instruction and Data Caches

The STM32F4 has 64-line instruction cache and 8-line data cache. Enable both along with the prefetch buffer to hide flash latency from the CPU. With all caches enabled and code running from flash at 168 MHz, effective IPC approaches what you would get with zero-wait-state flash.

```c
/* Configure flash before increasing clock speed */
FLASH->ACR = FLASH_ACR_LATENCY_5WS   // 5 wait states for 168 MHz
           | FLASH_ACR_PRFTEN        // enable prefetch
           | FLASH_ACR_ICEN          // enable instruction cache
           | FLASH_ACR_DCEN;         // enable data cache

/* Verify it was written (flash ACR may be sticky) */
while ((FLASH->ACR & FLASH_ACR_LATENCY) != FLASH_ACR_LATENCY_5WS);
```

### Power Voltage Scaling

The STM32F4 voltage regulator has three scale modes:

| Scale | Max frequency | Power |
|-------|--------------|-------|
| Scale 1 (default) | 168 MHz | Highest |
| Scale 2 | 144 MHz | Intermediate |
| Scale 3 | 120 MHz | Lowest |

At 168 MHz you must be in Scale 1. This is the default after reset, so usually no action is needed — but if you are waking from a low-power mode that changed the scale, restore it before increasing frequency:

```c
/* Ensure voltage regulator is in Scale 1 */
RCC->APB1ENR |= RCC_APB1ENR_PWREN;   // enable PWR clock
PWR->CR = (PWR->CR & ~PWR_CR_VOS) | PWR_CR_VOS;  // Scale 1
```

### Order of Operations for Clock Speed Increase

```
1. Set flash latency (higher value first)
2. Enable power regulator scale (Scale 1)
3. Enable HSE (if used) and wait for HSERDY
4. Configure PLL parameters (PLLCFGR)
5. Enable PLL and wait for PLLRDY
6. Configure AHB/APB prescalers
7. Switch SYSCLK to PLL
8. Confirm switch via SWS field
```

### Order of Operations for Clock Speed Decrease

When *decreasing* SYSCLK, the order reverses for flash latency:

```
1. Switch SYSCLK to slower source
2. Confirm via SWS
3. Reduce flash wait states (lower value after switching)
```

Reducing wait states before reducing clock speed would cause the CPU to read flash before it is ready.

---

## 12. RCC Registers In Depth

### RCC_CR — Clock Control Register (offset 0x00)

| Bit | Field | Description |
|-----|-------|-------------|
| 0 | `HSION` | Enable HSI oscillator |
| 1 | `HSIRDY` | HSI ready (read-only) |
| [7:3] | `HSITRIM[4:0]` | HSI trimming value |
| [15:8] | `HSICAL[7:0]` | HSI calibration (read-only, factory set) |
| 16 | `HSEON` | Enable HSE oscillator |
| 17 | `HSERDY` | HSE ready (read-only) |
| 18 | `HSEBYP` | HSE bypass (external clock input) |
| 19 | `CSSON` | Enable Clock Security System |
| 24 | `PLLON` | Enable main PLL |
| 25 | `PLLRDY` | PLL locked (read-only) |
| 26 | `PLLI2SON` | Enable PLLI2S |
| 27 | `PLLI2SRDY` | PLLI2S locked (read-only) |

### RCC_PLLCFGR — PLL Configuration Register (offset 0x04)

| Bits | Field | Description |
|------|-------|-------------|
| [5:0] | `PLLM[5:0]` | PLL input prescaler (2–63) |
| [14:6] | `PLLN[8:0]` | PLL multiplication factor (50–432) |
| [17:16] | `PLLP[1:0]` | PLL output division (00=÷2, 01=÷4, 10=÷6, 11=÷8) |
| 22 | `PLLSRC` | PLL source (0=HSI, 1=HSE) |
| [27:24] | `PLLQ[3:0]` | PLL division for USB/SDIO/RNG (2–15) |

### RCC_CFGR — Clock Configuration Register (offset 0x08)

| Bits | Field | Description |
|------|-------|-------------|
| [1:0] | `SW[1:0]` | System clock switch (00=HSI, 01=HSE, 10=PLL) |
| [3:2] | `SWS[1:0]` | System clock switch status (read-only) |
| [7:4] | `HPRE[3:0]` | AHB prescaler |
| [12:10] | `PPRE1[2:0]` | APB1 prescaler |
| [15:13] | `PPRE2[2:0]` | APB2 prescaler |
| [20:16] | `RTCPRE[4:0]` | HSE division for RTC (2–31) |
| [22:21] | `MCO1[1:0]` | MCO1 source select |
| 23 | `I2SSRC` | I2S clock source |
| [24:24] | `MCO1PRE[2:0]` | MCO1 prescaler |
| [27:25] | `MCO2PRE[2:0]` | MCO2 prescaler |
| [29:28] | `MCO2[1:0]` | MCO2 source select |

### RCC_CIR — Clock Interrupt Register (offset 0x0C)

| Bit | Field | Description |
|-----|-------|-------------|
| 0 | `LSIRDYF` | LSI ready interrupt flag |
| 1 | `LSERDYF` | LSE ready interrupt flag |
| 2 | `HSIRDYF` | HSI ready interrupt flag |
| 3 | `HSERDYF` | HSE ready interrupt flag |
| 4 | `PLLRDYF` | PLL ready interrupt flag |
| 7 | `CSSF` | Clock security system failure flag |
| [12:8] | `xRDYIE` | Ready interrupt enable bits |
| 23 | `CSSC` | CSS flag clear (write 1 to clear) |
| [20:16] | `xRDYC` | Ready flag clear bits |

### RCC_AHB1ENR — AHB1 Peripheral Clock Enable (offset 0x30)

| Bit | Peripheral |
|-----|-----------|
| 0 | GPIOA |
| 1 | GPIOB |
| 2 | GPIOC |
| 3 | GPIOD |
| 4 | GPIOE |
| 5 | GPIOF |
| 6 | GPIOG |
| 7 | GPIOH |
| 8 | GPIOI |
| 21 | DMA1 |
| 22 | DMA2 |
| 25 | ETHMAC |
| 29 | OTGHS |

### RCC_APB1ENR — APB1 Peripheral Clock Enable (offset 0x40)

| Bit | Peripheral | Bit | Peripheral |
|-----|-----------|-----|-----------|
| 0 | TIM2 | 17 | USART2 |
| 1 | TIM3 | 18 | USART3 |
| 2 | TIM4 | 19 | UART4 |
| 3 | TIM5 | 20 | UART5 |
| 4 | TIM6 | 21 | I2C1 |
| 5 | TIM7 | 22 | I2C2 |
| 6 | TIM12 | 23 | I2C3 |
| 7 | TIM13 | 25 | CAN1 |
| 8 | TIM14 | 26 | CAN2 |
| 11 | WWDG | 28 | PWR |
| 14 | SPI2 | 29 | DAC |
| 15 | SPI3 | — | — |

### RCC_APB2ENR — APB2 Peripheral Clock Enable (offset 0x44)

| Bit | Peripheral | Bit | Peripheral |
|-----|-----------|-----|-----------|
| 0 | TIM1 | 12 | SPI1 |
| 1 | TIM8 | 13 | SPI4 |
| 4 | USART1 | 14 | SYSCFG |
| 5 | USART6 | 16 | TIM9 |
| 8 | ADC1 | 17 | TIM10 |
| 9 | ADC2 | 18 | TIM11 |
| 10 | ADC3 | 20 | SPI5 |
| 11 | SDIO | 21 | SPI6 |

### RCC_BDCR — Backup Domain Control Register (offset 0x70)

| Bit | Field | Description |
|-----|-------|-------------|
| 0 | `LSEON` | Enable LSE oscillator |
| 1 | `LSERDY` | LSE ready (read-only) |
| 2 | `LSEBYP` | LSE bypass (external 32 kHz input) |
| [9:8] | `LSEDRV` | LSE oscillator drive strength |
| [9:8] | `RTCSEL` | RTC clock source (01=LSE, 10=LSI, 11=HSE/RTCPRE) |
| 15 | `RTCEN` | Enable RTC |
| 16 | `BDRST` | Backup domain reset |

### RCC_CSR — Control/Status Register (offset 0x74)

| Bit | Field | Description |
|-----|-------|-------------|
| 0 | `LSION` | Enable LSI oscillator |
| 1 | `LSIRDY` | LSI ready (read-only) |
| 24 | `RMVF` | Remove reset flags (write 1 to clear all flags) |
| 25 | `BORRSTF` | BOR reset flag |
| 26 | `PINRSTF` | Pin reset flag (NRST) |
| 27 | `PORRSTF` | POR/PDR reset flag |
| 28 | `SFTRSTF` | Software reset flag |
| 29 | `IWDGRSTF` | Independent watchdog reset flag |
| 30 | `WWDGRSTF` | Window watchdog reset flag |
| 31 | `LPWRRSTF` | Low-power reset flag |

---

## 13. Step-by-Step: Full 168 MHz PLL Setup

This is the complete, production-ready clock initialization sequence for an STM32F407 running at 168 MHz from an 8 MHz HSE crystal.

```c
#include "stm32f4xx.h"

/**
 * SystemClock_Config
 *
 * Configures:
 *   SYSCLK = 168 MHz  (from PLL, sourced by 8 MHz HSE)
 *   HCLK   = 168 MHz  (AHB, ÷1)
 *   PCLK1  =  42 MHz  (APB1, ÷4) → TIM2–7 input = 84 MHz
 *   PCLK2  =  84 MHz  (APB2, ÷2) → TIM1, TIM8 input = 168 MHz
 *   USB    =  48 MHz  (PLLQ = 336 / 7)
 *
 * PLL: PLLM=8, PLLN=336, PLLP=2, PLLQ=7
 */
void SystemClock_Config(void)
{
    /*------------------------------------------------------------------
     * 1. Enable power controller and set voltage regulator to Scale 1
     *    (required for 168 MHz operation)
     *------------------------------------------------------------------*/
    RCC->APB1ENR |= RCC_APB1ENR_PWREN;
    PWR->CR = (PWR->CR & ~PWR_CR_VOS) | PWR_CR_VOS;  // Scale 1

    /*------------------------------------------------------------------
     * 2. Set flash latency BEFORE increasing clock speed
     *    At 168 MHz / 3.3 V: 5 wait states required
     *------------------------------------------------------------------*/
    FLASH->ACR = FLASH_ACR_LATENCY_5WS
               | FLASH_ACR_PRFTEN      // prefetch buffer
               | FLASH_ACR_ICEN        // instruction cache
               | FLASH_ACR_DCEN;       // data cache

    /* Verify flash latency was applied */
    while ((FLASH->ACR & FLASH_ACR_LATENCY) != FLASH_ACR_LATENCY_5WS);

    /*------------------------------------------------------------------
     * 3. Enable HSE oscillator and wait for it to stabilise
     *------------------------------------------------------------------*/
    RCC->CR |= RCC_CR_HSEON;

    uint32_t timeout = 100000;
    while (!(RCC->CR & RCC_CR_HSERDY) && --timeout);
    if (!timeout) {
        /* HSE failed — fall back to HSI-based PLL or halt */
        Error_Handler();
    }

    /*------------------------------------------------------------------
     * 4. Configure the PLL (must be done while PLL is off)
     *    PLLM=8, PLLN=336, PLLP=÷2 (0b00), PLLQ=7
     *    VCO input  = 8 MHz / 8 = 1 MHz
     *    VCO output = 1 MHz × 336 = 336 MHz
     *    SYSCLK     = 336 MHz / 2 = 168 MHz
     *    USBCLK     = 336 MHz / 7 = 48 MHz
     *------------------------------------------------------------------*/
    RCC->PLLCFGR = (8U)                         /* PLLM = 8       */
                 | (336U << RCC_PLLCFGR_PLLN_Pos) /* PLLN = 336     */
                 | (0U   << RCC_PLLCFGR_PLLP_Pos) /* PLLP = ÷2 (0) */
                 | RCC_PLLCFGR_PLLSRC_HSE        /* source = HSE   */
                 | (7U   << RCC_PLLCFGR_PLLQ_Pos); /* PLLQ = 7      */

    /*------------------------------------------------------------------
     * 5. Enable the PLL and wait for lock
     *------------------------------------------------------------------*/
    RCC->CR |= RCC_CR_PLLON;
    while (!(RCC->CR & RCC_CR_PLLRDY));

    /*------------------------------------------------------------------
     * 6. Configure AHB and APB bus prescalers
     *    AHB  = ÷1 = 168 MHz
     *    APB1 = ÷4 =  42 MHz  (max 42 MHz)
     *    APB2 = ÷2 =  84 MHz  (max 84 MHz)
     *------------------------------------------------------------------*/
    RCC->CFGR = (RCC->CFGR & ~(RCC_CFGR_HPRE | RCC_CFGR_PPRE1 | RCC_CFGR_PPRE2))
              | RCC_CFGR_HPRE_DIV1
              | RCC_CFGR_PPRE1_DIV4
              | RCC_CFGR_PPRE2_DIV2;

    /*------------------------------------------------------------------
     * 7. Switch SYSCLK source to PLL
     *------------------------------------------------------------------*/
    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_PLL;

    /* 8. Confirm the switch via SWS field */
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL);

    /*------------------------------------------------------------------
     * 9. Update SystemCoreClock variable (used by CMSIS and HAL)
     *------------------------------------------------------------------*/
    SystemCoreClock = 168000000UL;
}
```

### HSI Fallback Version (no crystal)

```c
void SystemClock_Config_HSI(void)
{
    /* Flash and voltage setup — same as above */
    RCC->APB1ENR |= RCC_APB1ENR_PWREN;
    PWR->CR = (PWR->CR & ~PWR_CR_VOS) | PWR_CR_VOS;
    FLASH->ACR = FLASH_ACR_LATENCY_5WS | FLASH_ACR_PRFTEN
               | FLASH_ACR_ICEN | FLASH_ACR_DCEN;
    while ((FLASH->ACR & FLASH_ACR_LATENCY) != FLASH_ACR_LATENCY_5WS);

    /* HSI is already on at reset — just verify it */
    while (!(RCC->CR & RCC_CR_HSIRDY));

    /* PLL from HSI: PLLM=16, PLLN=336, PLLP=÷2, PLLQ=7 */
    RCC->PLLCFGR = (16U)
                 | (336U << RCC_PLLCFGR_PLLN_Pos)
                 | (0U   << RCC_PLLCFGR_PLLP_Pos)
                 | (0U   << 22)                    /* PLLSRC = HSI */
                 | (7U   << RCC_PLLCFGR_PLLQ_Pos);

    RCC->CR |= RCC_CR_PLLON;
    while (!(RCC->CR & RCC_CR_PLLRDY));

    RCC->CFGR = (RCC->CFGR & ~(RCC_CFGR_HPRE | RCC_CFGR_PPRE1 | RCC_CFGR_PPRE2))
              | RCC_CFGR_HPRE_DIV1
              | RCC_CFGR_PPRE1_DIV4
              | RCC_CFGR_PPRE2_DIV2;

    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL);

    SystemCoreClock = 168000000UL;
}
```

---

## 14. Peripheral Clock Enable and Reset

Every peripheral on the STM32F4 has its clock gated — disabled by default to save power. Before accessing any peripheral register, its clock must be enabled in the appropriate `RCC_AHBxENR` or `RCC_APBxENR` register.

### Clock Enable Pattern

```c
/* Enable a peripheral clock */
RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;

/* Wait one bus cycle for the clock to propagate before accessing registers.
   This is the "read-back" idiom: */
(void)RCC->APB1ENR;

/* Now safe to configure TIM2 */
TIM2->PSC = 83;
```

The read-back forces the CPU to wait for the write to complete through the AHB bus before proceeding. Without it, on some bus configurations the first register access to the newly-clocked peripheral can return incorrect values.

### Peripheral Reset

The RCC can reset any peripheral to its default state by pulsing the peripheral reset bit:

```c
/* Reset USART2 */
RCC->APB1RSTR |= RCC_APB1RSTR_USART2RST;  // assert reset
RCC->APB1RSTR &= ~RCC_APB1RSTR_USART2RST; // release reset
/* USART2 is now in the same state as after power-on */
```

This is useful for re-initializing a peripheral without resetting the entire system, or for recovering from a peripheral fault.

### Low-Power Clock Enable Registers

`RCC_AHB1LPENR`, `RCC_APB1LPENR`, `RCC_APB2LPENR` control whether a peripheral clock is supplied during *Sleep* mode. By default, most peripherals retain their clock during Sleep. Disabling unused peripheral clocks in these registers reduces Sleep-mode current:

```c
/* Disable DMA1 clock during Sleep (if DMA not needed in sleep) */
RCC->AHB1LPENR &= ~RCC_AHB1LPENR_DMA1LPEN;
```

---

## 15. Clock Security System (CSS)

The Clock Security System (CSS) monitors the HSE oscillator. If HSE fails (crystal stops oscillating, PCB fault, power transient), the CSS:

1. Automatically switches SYSCLK back to HSI (16 MHz)
2. Sets the `CSSF` flag in `RCC_CIR`
3. Generates a Non-Maskable Interrupt (NMI)

This gives the application an opportunity to detect and handle HSE failure rather than running at the wrong frequency silently.

### Enabling CSS

```c
/* CSS must be enabled after HSE is running */
RCC->CR |= RCC_CR_CSSON;
```

### CSS NMI Handler

```c
void NMI_Handler(void)
{
    if (RCC->CIR & RCC_CIR_CSSF) {
        /* HSE failure detected */
        RCC->CIR |= RCC_CIR_CSSC;  // clear CSS flag

        /* The CPU is now running from HSI at 16 MHz.
           Flash latency can be reduced.
           PLL is now sourced from HSI automatically.
           
           Options:
           1. Reconfigure PLL from HSI and continue degraded
           2. Disable all non-critical peripherals
           3. Set error flag and reboot
        */

        /* Reduce flash latency for HSI speed */
        FLASH->ACR = (FLASH->ACR & ~FLASH_ACR_LATENCY) | FLASH_ACR_LATENCY_0WS;

        /* Signal application layer */
        system_clock_failure = 1;
    }
}
```

### CSS Best Practices

- Always enable CSS in safety-critical applications
- Implement the NMI handler — without it, an HSE failure causes silent corruption
- After an HSE failure, log the event to non-volatile memory if possible
- Consider a watchdog that detects sustained degraded operation

---

## 16. MCO — Master Clock Output

The STM32F4 has two Master Clock Output pins (MCO1 and MCO2) that output an internal clock signal on a GPIO pin. This is invaluable for:

- Verifying clock configuration with an oscilloscope
- Providing a clock to external devices
- Debugging PLL lock issues

### MCO1 (PA8)

| MCO1[1:0] | Source |
|-----------|--------|
| `00` | HSI |
| `01` | LSE |
| `10` | HSE |
| `11` | PLL |

### MCO2 (PC9)

| MCO2[1:0] | Source |
|-----------|--------|
| `00` | SYSCLK |
| `01` | PLLI2S |
| `10` | HSE |
| `11` | PLL |

Both MCOs have a prescaler (MCO1PRE, MCO2PRE) that divides the output by 1–5 to keep the pin frequency within its GPIO speed capabilities.

### MCO Configuration Example

```c
/* Output HSE ÷ 1 on PA8 (MCO1) for oscilloscope verification */

/* 1. Configure PA8 as alternate function, very high speed */
RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
GPIOA->MODER   = (GPIOA->MODER  & ~GPIO_MODER_MODER8)  | GPIO_MODER_MODER8_1; // AF
GPIOA->OSPEEDR = (GPIOA->OSPEEDR & ~GPIO_OSPEEDR_OSPEED8) | GPIO_OSPEEDR_OSPEED8; // very high
GPIOA->AFR[1] |= (0 << 0);   // AF0 for MCO1

/* 2. Select MCO1 source and prescaler */
RCC->CFGR = (RCC->CFGR & ~(RCC_CFGR_MCO1 | RCC_CFGR_MCO1PRE))
           | (0x2U << RCC_CFGR_MCO1_Pos)    // MCO1 = HSE
           | (0x0U << RCC_CFGR_MCO1PRE_Pos); // ÷1 (no division)

/* PA8 now outputs the HSE frequency — verify with oscilloscope */
```

```c
/* Output SYSCLK ÷ 4 on PC9 (MCO2) */
RCC->AHB1ENR |= RCC_AHB1ENR_GPIOCEN;
GPIOC->MODER   = (GPIOC->MODER  & ~GPIO_MODER_MODER9)  | GPIO_MODER_MODER9_1;
GPIOC->OSPEEDR = (GPIOC->OSPEEDR& ~GPIO_OSPEEDR_OSPEED9)| GPIO_OSPEEDR_OSPEED9;
GPIOC->AFR[1] |= (0 << 4);   // AF0 for MCO2

RCC->CFGR = (RCC->CFGR & ~(RCC_CFGR_MCO2 | RCC_CFGR_MCO2PRE))
           | (0x0U << RCC_CFGR_MCO2_Pos)    // MCO2 = SYSCLK
           | (0x6U << RCC_CFGR_MCO2PRE_Pos); // ÷4 = 42 MHz output
```

> At 168 MHz SYSCLK, even with ÷4 the MCO outputs 42 MHz. Use a very-high-speed GPIO setting and keep the trace short. At ÷5 (maximum) you get 33.6 MHz.

---

## 17. RTC Clock Domain

The Real-Time Clock lives in a separate power domain (Vbat) that can remain powered even when the main 3.3 V supply is off, using a coin cell battery. The RTC clock source is independent of SYSCLK.

### RTC Clock Sources

| RTCSEL | Source | Accuracy |
|--------|--------|---------|
| `00` | No clock | — |
| `01` | LSE (32.768 kHz) | ±20–100 ppm |
| `10` | LSI (~32 kHz) | ±15% |
| `11` | HSE ÷ RTCPRE | Varies |

### RTC Clock Configuration

```c
void RTC_ClockSource_LSE(void)
{
    /* 1. Enable PWR and backup domain write access */
    RCC->APB1ENR |= RCC_APB1ENR_PWREN;
    PWR->CR      |= PWR_CR_DBP;        // disable backup domain protection

    /* 2. Reset backup domain only if clock source is not already configured.
          Resetting clears RTC registers — avoid on warm boot. */
    if ((RCC->BDCR & RCC_BDCR_RTCSEL) == 0) {
        RCC->BDCR |= RCC_BDCR_BDRST;   // reset backup domain
        RCC->BDCR &= ~RCC_BDCR_BDRST;  // release reset
    }

    /* 3. Enable LSE with medium-high drive */
    RCC->BDCR |= RCC_BDCR_LSEDRV_1;   // medium-high drive
    RCC->BDCR |= RCC_BDCR_LSEON;

    /* 4. Wait for LSE ready (can take 0.5–2 seconds) */
    uint32_t timeout = 10000000;
    while (!(RCC->BDCR & RCC_BDCR_LSERDY) && --timeout);
    if (!timeout) {
        /* LSE failed — fall back to LSI */
        LSI_fallback_RTC();
        return;
    }

    /* 5. Select LSE as RTC clock source */
    RCC->BDCR = (RCC->BDCR & ~RCC_BDCR_RTCSEL) | RCC_BDCR_RTCSEL_0; // 01 = LSE

    /* 6. Enable RTC */
    RCC->BDCR |= RCC_BDCR_RTCEN;
}
```

### Preserving RTC Across Resets

The backup domain retains its state through a system reset (not a power-on reset or BDRST). To avoid resetting RTC time on every reboot:

```c
/* Check if backup domain is already configured */
if ((RCC->BDCR & RCC_BDCR_RTCEN) && (RCC->BDCR & RCC_BDCR_RTCSEL_0)) {
    /* RTC was already running — skip reconfiguration */
    /* Just enable write access and continue */
    PWR->CR |= PWR_CR_DBP;
    return;
}
```

---

## 18. Low-Power Clock Considerations

### Sleep Mode

In Sleep mode, the CPU core halts but AHB/APB clocks continue. Peripherals (DMA, timers, UART receive) can operate and wake the core via interrupts. Clock configuration does not change in Sleep.

```c
__WFI();   // Wait For Interrupt — enter Sleep
```

### Stop Mode

In Stop mode, all clocks except LSI and LSE are stopped. The HSI and PLL are disabled. On wake-up, the device restarts from HSI at 16 MHz — **your clock configuration is lost** and must be re-applied.

```c
/* Before entering Stop */
PWR->CR |= PWR_CR_LPDS;   // low-power voltage regulator in stop

/* Enter Stop mode */
SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk;
PWR->CR  |= PWR_CR_PDDS;   // 0=Stop, 1=Standby — use 0 for Stop
__WFI();

/* After wake-up from Stop — CPU runs from HSI at 16 MHz */
SCB->SCR &= ~SCB_SCR_SLEEPDEEP_Msk;

/* MUST reconfigure clocks */
SystemClock_Config();
```

### Standby Mode

In Standby mode, the entire 1.2 V domain (CPU, SRAM, most peripherals) is powered off. Only the backup domain (RTC, backup registers) survives. Wake-up is equivalent to a reset. Full clock reconfiguration is required in the startup code.

### Minimising Clock-Related Power

```c
/* 1. Disable clocks to unused peripherals */
RCC->AHB1ENR  &= ~(RCC_AHB1ENR_GPIOFEN  |
                   RCC_AHB1ENR_GPIOGEN  |
                   RCC_AHB1ENR_GPIOHEN);  // disable unused GPIO ports

/* 2. Disable unused peripherals' Sleep-mode clocks */
RCC->AHB1LPENR &= ~RCC_AHB1LPENR_FLITFLPEN;  // flash interface (save ~2 mA)

/* 3. Reduce SYSCLK before entering sleep if throughput allows */
/* Switch PLL off, run from HSI at 16 MHz for background tasks */
```

---

## 19. Clock Measurement and Calibration

### Using TIM to Measure a Clock

Any timer can measure an unknown clock by counting edges over a known reference period:

```c
/* Measure LSI frequency using TIM5 and the SysTick reference */
uint32_t measure_lsi_frequency(void)
{
    /* Enable TIM5, remap CH4 to LSI */
    RCC->APB1ENR |= RCC_APB1ENR_TIM5EN;
    TIM5->OR = TIM5_OR_TI4_RMP;          // connect LSI to TIM5 CH4
    TIM5->CCMR2 = (0x1 << 8);            // IC4 mapped on TI4
    TIM5->CCER  = TIM_CCER_CC4E;         // enable capture 4
    TIM5->CR1   = TIM_CR1_CEN;           // start timer

    /* Wait for two captures */
    while (!(TIM5->SR & TIM_SR_CC4IF));
    TIM5->SR &= ~TIM_SR_CC4IF;
    uint32_t first = TIM5->CCR4;

    while (!(TIM5->SR & TIM_SR_CC4IF));
    uint32_t second = TIM5->CCR4;

    TIM5->CR1 = 0;   // stop

    /* TIM5 runs from PCLK1 × 2 = 84 MHz.
       LSI period in TIM5 ticks = (second - first).
       LSI frequency = 84,000,000 / (second - first) */
    uint32_t period = second - first;
    return 84000000UL / period;
}
```

### Using MCO + External Frequency Counter

Output any internal clock on MCO1 or MCO2 and measure it with an oscilloscope or frequency counter. This is the simplest way to verify PLL configuration.

```c
/* Output PLLCLK ÷ 5 on MCO2 to verify 168 MHz operation */
/* Expected MCO2 frequency = 168 / 5 = 33.6 MHz */
RCC->CFGR = (RCC->CFGR & ~(RCC_CFGR_MCO2 | RCC_CFGR_MCO2PRE))
           | (0x3U << RCC_CFGR_MCO2_Pos)    // MCO2 = PLL
           | (0x7U << RCC_CFGR_MCO2PRE_Pos); // ÷5
```

### Verifying Peripheral Clocks from Software

The `SystemCoreClock` variable (updated by CMSIS or your code) reflects HCLK. Compute derived clocks:

```c
uint32_t get_pclk1(void)
{
    uint32_t hclk = SystemCoreClock;
    uint32_t ppre1 = (RCC->CFGR & RCC_CFGR_PPRE1) >> RCC_CFGR_PPRE1_Pos;
    uint32_t div = (ppre1 & 0x4) ? (2U << (ppre1 & 0x3)) : 1U;
    return hclk / div;
}

uint32_t get_tim_clock_apb1(void)
{
    uint32_t ppre1 = (RCC->CFGR & RCC_CFGR_PPRE1) >> RCC_CFGR_PPRE1_Pos;
    uint32_t multiplier = (ppre1 & 0x4) ? 2U : 1U;  // double if prescaler ≠ 1
    return get_pclk1() * multiplier;
}
```

---

## 20. Common Mistakes and Pitfalls

### Mistake 1: Setting Clock Speed Before Flash Latency

Increasing SYSCLK without first setting flash wait states causes the CPU to execute corrupted instructions. The system hangs or behaves randomly.

```c
/* WRONG — crashes at 168 MHz */
RCC->CFGR |= RCC_CFGR_SW_PLL;           // switch before flash ready
FLASH->ACR |= FLASH_ACR_LATENCY_5WS;   // too late

/* CORRECT */
FLASH->ACR |= FLASH_ACR_LATENCY_5WS;   // flash first
while (...) ;                            // verify latency
RCC->CFGR |= RCC_CFGR_SW_PLL;          // then switch
```

### Mistake 2: Not Waiting for Clock Ready Flags

Enabling an oscillator and immediately using it — without polling the `RDY` flag — may work on the bench but fails in production where temperature extremes slow crystal startup.

```c
/* WRONG */
RCC->CR |= RCC_CR_HSEON;
RCC->PLLCFGR |= RCC_PLLCFGR_PLLSRC_HSE;  // HSE may not be ready yet

/* CORRECT */
RCC->CR |= RCC_CR_HSEON;
while (!(RCC->CR & RCC_CR_HSERDY));        // wait
RCC->PLLCFGR |= RCC_PLLCFGR_PLLSRC_HSE;  // now safe
```

### Mistake 3: Not Checking SWS After SW Switch

The clock switch takes a few cycles. Code that assumes the new clock is immediately active can run at the wrong speed momentarily.

```c
/* WRONG */
RCC->CFGR |= RCC_CFGR_SW_PLL;
do_something_speed_sensitive();   // might still be on HSI

/* CORRECT */
RCC->CFGR |= RCC_CFGR_SW_PLL;
while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL);
do_something_speed_sensitive();   // guaranteed PLL active
```

### Mistake 4: Wrong Timer Clock Assumption

The timer doubling rule catches many developers. If APB1 prescaler ≠ 1, timer input clock = 2 × PCLK1.

```c
/* WRONG: assume TIM2 clock = PCLK1 = 42 MHz */
TIM2->PSC = 42000 - 1;   // 42 MHz / 42000 = 1 kHz? No: 84000 - 1 is needed

/* CORRECT: TIM2 clock = 84 MHz (doubled) */
TIM2->PSC = 84000 - 1;   // 84 MHz / 84000 = 1 kHz ✓
```

### Mistake 5: Not Enabling the Peripheral Clock Before Configuring It

Accessing peripheral registers without the clock enabled reads zeros and writes are silently discarded. Or on some variants it triggers a hard fault.

```c
/* WRONG */
USART2->BRR = 0x683;   // clock not enabled — write is lost

/* CORRECT */
RCC->APB1ENR |= RCC_APB1ENR_USART2EN;
(void)RCC->APB1ENR;    // clock settle
USART2->BRR = 0x683;   // now it works
```

### Mistake 6: Modifying PLL While It Is Running

PLL parameters cannot be changed while the PLL is enabled. The hardware ignores writes to `PLLCFGR` while `PLLON` is set.

```c
/* WRONG */
RCC->CR |= RCC_CR_PLLON;
RCC->PLLCFGR = new_config;   // ignored — PLL already running

/* CORRECT */
RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_HSI;  // switch away from PLL
while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_HSI);
RCC->CR &= ~RCC_CR_PLLON;          // disable PLL
while (RCC->CR & RCC_CR_PLLRDY);   // wait until unlocked
RCC->PLLCFGR = new_config;         // now safe to write
RCC->CR |= RCC_CR_PLLON;           // re-enable
while (!(RCC->CR & RCC_CR_PLLRDY));
```

### Mistake 7: Forgetting to Reconfigure Clocks After Stop Mode

Stop mode disables all clocks. On wake-up the device runs from HSI at 16 MHz. Peripheral baud rates, PWM frequencies, and timers all run at incorrect rates until clocks are re-initialized.

```c
/* Stop mode exit ISR or early startup */
void EXTI0_IRQHandler(void)
{
    EXTI->PR = EXTI_PR_PR0;
    SystemClock_Config();    // MUST reconfigure — back on HSI at 16 MHz otherwise
}
```

### Mistake 8: Accessing BDCR Without Disabling Write Protection

The backup domain registers are write-protected to prevent accidental modification. Forgetting `PWR_CR_DBP` means LSE and RTC configuration writes are silently ignored.

```c
/* WRONG */
RCC->BDCR |= RCC_BDCR_LSEON;   // ignored — write protected

/* CORRECT */
RCC->APB1ENR |= RCC_APB1ENR_PWREN;
PWR->CR      |= PWR_CR_DBP;     // unlock backup domain
RCC->BDCR    |= RCC_BDCR_LSEON; // now works
```

---

## 21. Quick Reference Tables

### PLL Parameter Ranges (STM32F407)

| Parameter | Min | Max | Notes |
|-----------|-----|-----|-------|
| PLLM | 2 | 63 | VCO input = PLL_src / PLLM: must be 1–2 MHz |
| PLLN | 50 | 432 | VCO output = VCO_in × PLLN: must be 100–432 MHz |
| PLLP | 2 | 8 | Only 2, 4, 6, 8 are valid (encoded as 0, 1, 2, 3) |
| PLLQ | 2 | 15 | USB needs exactly 48 MHz |
| VCO input | 1 MHz | 2 MHz | |
| VCO output | 100 MHz | 432 MHz | |
| SYSCLK | — | 168 MHz | F407; 180 MHz on F429 |

### Common PLL Configurations

| HSE | Target SYSCLK | PLLM | PLLN | PLLP | PLLQ | USB OK? |
|-----|--------------|------|------|------|------|---------|
| 8 MHz | 168 MHz | 8 | 336 | 2 | 7 | Yes (48 MHz) |
| 8 MHz | 120 MHz | 8 | 240 | 2 | 5 | Yes (48 MHz) |
| 8 MHz | 96 MHz | 8 | 192 | 2 | 4 | Yes (48 MHz) |
| 8 MHz | 84 MHz | 8 | 336 | 4 | 7 | Yes (48 MHz) |
| 16 MHz (HSI) | 168 MHz | 16 | 336 | 2 | 7 | Yes (48 MHz) |
| 25 MHz | 168 MHz | 25 | 336 | 2 | 7 | Yes (48 MHz) |
| 12 MHz | 168 MHz | 6 | 168 | 1*| 7 | Yes* |

*PLLP=1 is invalid; adjust PLLN accordingly.

### Flash Latency at 3.3 V (STM32F407)

| SYSCLK | Wait States | LATENCY |
|--------|------------|---------|
| 0–30 MHz | 0 WS | `0b000` |
| 30–60 MHz | 1 WS | `0b001` |
| 60–90 MHz | 2 WS | `0b010` |
| 90–120 MHz | 3 WS | `0b011` |
| 120–150 MHz | 4 WS | `0b100` |
| 150–168 MHz | 5 WS | `0b101` |

### Clock Frequencies at 168 MHz SYSCLK

| Clock | Frequency | Source |
|-------|-----------|--------|
| SYSCLK | 168 MHz | PLLP output |
| HCLK (AHB) | 168 MHz | SYSCLK ÷ 1 |
| PCLK1 (APB1) | 42 MHz | HCLK ÷ 4 |
| PCLK2 (APB2) | 84 MHz | HCLK ÷ 2 |
| TIM2–7, TIM12–14 | 84 MHz | 2 × PCLK1 |
| TIM1, TIM8–11 | 168 MHz | 2 × PCLK2 |
| USB OTG FS | 48 MHz | PLLQ (÷7) |
| SysTick (default) | 21 MHz | HCLK ÷ 8 |

### Clock Source Selection Summary

| Requirement | Recommended source |
|-------------|-------------------|
| Boot / fallback | HSI (16 MHz) |
| Maximum CPU speed | HSE → PLL |
| USB FS | HSE → PLL (PLLQ = 48 MHz) |
| RTC, long-term timekeeping | LSE (32.768 kHz crystal) |
| IWDG (independent watchdog) | LSI |
| Low-power deep sleep | LSI or LSE |
| I2S audio (precise sample rate) | PLLI2S |

### Key Bit Positions in RCC_CFGR

| Field | Bit position | CMSIS macro |
|-------|-------------|-------------|
| SW | [1:0] | `RCC_CFGR_SW_Pos` |
| SWS | [3:2] | `RCC_CFGR_SWS_Pos` |
| HPRE | [7:4] | `RCC_CFGR_HPRE_Pos` |
| PPRE1 | [12:10] | `RCC_CFGR_PPRE1_Pos` |
| PPRE2 | [15:13] | `RCC_CFGR_PPRE2_Pos` |
| MCO1 | [22:21] | `RCC_CFGR_MCO1_Pos` |
| MCO2 | [31:30] | `RCC_CFGR_MCO2_Pos` |

---

*This document covers the STM32F4 RCC peripheral (RM0090 reference manual). Always consult the Reference Manual and your specific device's datasheet for the authoritative register map — some fields differ between F401, F407, F411, F429, and F446 variants.*
