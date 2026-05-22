#include "FreeRTOS.h"
#include "task.h"
#include "stm32f4xx.h"

static void vTask1(void *pvParameters){
    while(1){
        GPIOA->ODR ^= (1 << 5);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static void vTask2(void *pvParameters){
    volatile int b = 0;
    while(1){
        b++;
    }
}

extern uint32_t _sdata; 
extern uint32_t _edata; 
extern uint32_t _sidata; 
extern uint32_t _sbss; 
extern uint32_t _ebss; 

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

void led_setup(){
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    GPIOA->MODER &= ~(GPIO_MODER_MODER5);
    GPIOA->MODER |= GPIO_MODER_MODER5_0;
    GPIOA->OTYPER &= ~(GPIO_OTYPER_OT5);
    GPIOA->PUPDR  &= ~(GPIO_PUPDR_PUPD5);
    GPIOA->BSRR = GPIO_BSRR_BS5;
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

void start(){
    
    init_mem();
    clock_init();
    led_setup();

    BaseType_t xReturn;
    xReturn = xTaskCreate(vTask1, "T1", configMINIMAL_STACK_SIZE, NULL, 1, NULL);
    xReturn = xTaskCreate(vTask2, "T2", configMINIMAL_STACK_SIZE, NULL, 1, NULL);
    vTaskStartScheduler();
    return;
}
