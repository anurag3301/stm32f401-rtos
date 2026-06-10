#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"
#include "gpio.hpp"
#include "uart.hpp"

static GPIO* led    = nullptr;
static GPIO* button = nullptr;

static void vTask1(void *pvParameters){
    GPIO led2 (GPIOC, 11, GPIO::Mode::Output);
    while(1){
        led2.toggle();
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

static void vTask2(void *pvParameters){
    UART uart(USART2, {GPIOA, 2}, {GPIOA, 3});
    uart.setStdout();
    volatile uint16_t c = 1;
    while(1){
        printf("Hello world: %d\n\r", c++);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void main(){
    led    = new GPIO(GPIOC, 10, GPIO::Mode::Output);
    button = new GPIO(GPIOC, 12, GPIO::Mode::Input,
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
}
