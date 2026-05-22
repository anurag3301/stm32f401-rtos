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

void start(){
    
    init_mem();
    led_setup();

    BaseType_t xReturn;
    xReturn = xTaskCreate(vTask1, "T1", configMINIMAL_STACK_SIZE, NULL, 1, NULL);
    xReturn = xTaskCreate(vTask2, "T2", configMINIMAL_STACK_SIZE, NULL, 1, NULL);
    vTaskStartScheduler();
    return;
}
