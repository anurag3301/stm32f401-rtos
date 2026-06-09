#include "FreeRTOS.h"
#include "task.h"
#include "stm32f4xx.h"
#include "gpio.hpp"

static GPIO* led    = nullptr;
static GPIO* button = nullptr;

static void vTask1(void *pvParameters){
    while(1){
        vTaskDelay(pdMS_TO_TICKS(5000));
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



void uart_setup(){
    //  PA2 = USART2 TX
    //  PA3 = USART2 RX
    GPIO tx (GPIOA_BASE, 2,  GPIO::Mode::Alternate, 
             GPIO::OutputType::PushPull,  GPIO::Speed::High, 
             GPIO::Pull::PullUp,  GPIO::AlternateFunction::AF7);
    GPIO rx (GPIOA_BASE, 3,  GPIO::Mode::Alternate, 
             GPIO::OutputType::OpenDrain, GPIO::Speed::High, 
             GPIO::Pull::NoPUD,   GPIO::AlternateFunction::AF7);

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
static void on_button_press(void* param) {
    GPIO* led = static_cast<GPIO*>(param);
    led->toggle(); 
}

extern "C" void start(){
    
    init_mem();
    clock_init();

    led    = new GPIO(GPIOC_BASE, 10, GPIO::Mode::Output);
    button = new GPIO(GPIOC_BASE, 12, GPIO::Mode::Input,
                      GPIO::OutputType::None,
                      GPIO::Speed::None,
                      GPIO::Pull::PullUp);

    button->setInterruptCallback(GPIO::Edge::Fall, +[](void* param) {
        static TickType_t last_tick = 0;
        TickType_t now = xTaskGetTickCountFromISR();
        if ((now - last_tick) > pdMS_TO_TICKS(200)) {
            last_tick = now;
            GPIO* led = static_cast<GPIO*>(param);
            led->toggle();
        }
    }, led);

    BaseType_t xReturn;
    xReturn = xTaskCreate(vTask1, "T1", configMINIMAL_STACK_SIZE, NULL, 1, NULL);
    xReturn = xTaskCreate(vTask2, "T2", configMINIMAL_STACK_SIZE, NULL, 1, NULL);
    vTaskStartScheduler();
    return;
}
