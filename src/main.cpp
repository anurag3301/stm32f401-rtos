#include "FreeRTOS.h"
#include "task.h"
#include "gpio.hpp"

static GPIO* led    = nullptr;
static GPIO* button = nullptr;

static void vTask1(void *pvParameters){
    GPIO led2 (GPIOC_BASE, 11, GPIO::Mode::Output);
    while(1){
        led2.toggle();
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

static void vTask2(void *pvParameters){
    volatile int b = 0;
    while(1){
        b++;
    }
}

void main(){
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
}
