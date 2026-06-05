#include "gpio.hpp"

namespace GPIO{

bool inuse[5][16] = {false};

int8_t portToIdx(uint32_t portBase){
    switch (portBase){
        case GPIOA_BASE: return 0;
        case GPIOB_BASE: return 1;
        case GPIOC_BASE: return 2;
        case GPIOD_BASE: return 3;
        case GPIOE_BASE: return 4;
        default: return -1;
    }
}

void enableClock(uint32_t portBase){
    switch (portBase){
        case GPIOA_BASE: 
            RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
            break;
        case GPIOB_BASE:
            RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN;
            break;
        case GPIOC_BASE:
            RCC->AHB1ENR |= RCC_AHB1ENR_GPIOCEN;
            break;
        case GPIOD_BASE:
            RCC->AHB1ENR |= RCC_AHB1ENR_GPIODEN;
            break;
        case GPIOE_BASE:
            RCC->AHB1ENR |= RCC_AHB1ENR_GPIOEEN;
            break;
    }
}
}