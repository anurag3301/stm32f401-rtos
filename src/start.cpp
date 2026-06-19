#include "gpio.hpp"

extern void main();

extern uint32_t _sdata; 
extern uint32_t _edata; 
extern uint32_t _sidata; 
extern uint32_t _sbss; 
extern uint32_t _ebss; 
uint32_t SystemCoreClock = 84000000;

void init_mem(){
    uint32_t *src = &_sidata;
    uint32_t *dst = &_sdata;
    while (dst < &_edata){
        *dst++ = *src++;
    }

    dst = &_sbss;
    while(dst < &_ebss){
        *dst++ = 0;
    }
}

void clock_init(void){
    RCC->CR |= RCC_CR_HSION;
    while(!(RCC->CR & RCC_CR_HSIRDY));

    RCC->PLLCFGR = (16 << RCC_PLLCFGR_PLLM_Pos) |
                   (336 << RCC_PLLCFGR_PLLN_Pos) |
                   (1 << RCC_PLLCFGR_PLLP_Pos) |
                   RCC_PLLCFGR_PLLSRC_HSI;

    RCC->CR |= RCC_CR_PLLON;
    while(!(RCC->CR & RCC_CR_PLLRDY));
    FLASH->ACR = FLASH_ACR_ICEN | FLASH_ACR_DCEN | FLASH_ACR_LATENCY_2WS;
    RCC->CFGR &= ~RCC_CFGR_SW;
    RCC->CFGR |= RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL);
}

static void pc13_init_high() {
    RCC->AHB1ENR  |= RCC_AHB1ENR_GPIOCEN;
    GPIOC->MODER  &= ~(3U << (13 * 2));
    GPIOC->MODER  |=  (1U << (13 * 2)); // output
    GPIOC->BSRR    =  (1U << 13);        // set (drive high)
}

extern "C" __attribute__((noreturn)) void fault_handler() {
    GPIOC->BSRR = (1U << (13 + 16)); // reset (drive low)
    while (1);
}

extern "C" void start(){
    pc13_init_high();
    init_mem();
    clock_init();
    main();
}
