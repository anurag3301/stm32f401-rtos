#include "gpio.hpp"

bool GPIO::inuse[5][16] = {};
Callback GPIO::interrupt_callbacks[16] = {};

GPIO::GPIO(uint32_t portBase, uint8_t pin,
           Mode mode, OutputType otype, Speed speed,
           Pull pull, AlternateFunction af)
    : _portBase(portBase), _pin(pin), _inuseIdx(-1)
{
    _inuseIdx = portToIdx(portBase);
    if (_inuseIdx == -1 || inuse[_inuseIdx][_pin]) return;

    enableClock(portBase);
    setMode(mode);
    setOutputType(otype);
    setSpeed(speed);
    setPull(pull);
    setAlternateFunction(af);

    inuse[_inuseIdx][_pin] = true;
}

GPIO::GPIO(GPIO&& other) noexcept
    : _portBase(other._portBase), _pin(other._pin), _inuseIdx(other._inuseIdx)
{
    other._inuseIdx = -1;
}

GPIO& GPIO::operator=(GPIO&& other) noexcept {
    if (this != &other) {
        this->~GPIO();
        _portBase  = other._portBase;
        _pin       = other._pin;
        _inuseIdx  = other._inuseIdx;
        other._inuseIdx = -1;
    }
    return *this;
}

GPIO::~GPIO() {
    if (_inuseIdx != -1) {
        inuse[_inuseIdx][_pin] = false;

        if (interrupt_callbacks[_pin].fn != nullptr) {
            EXTI->IMR  &= ~(1U << _pin);
            EXTI->RTSR &= ~(1U << _pin);
            EXTI->FTSR &= ~(1U << _pin);
            EXTI->PR    =  (1U << _pin);

            // Disable NVIC only if no other pin sharing this IRQ is active
            // EXTI9_5 and EXTI15_10 are shared lines
            auto anyActive = [](int lo, int hi) {
                for (int i = lo; i <= hi; i++)
                    if (interrupt_callbacks[i].fn != nullptr) return true;
                return false;
            };

            if (_pin <= 4) {
                const IRQn_Type irqs[] = {
                    EXTI0_IRQn, EXTI1_IRQn, EXTI2_IRQn, EXTI3_IRQn, EXTI4_IRQn
                };
                NVIC_DisableIRQ(irqs[_pin]);
            } else if (_pin <= 9) {
                if (!anyActive(5, 9)) NVIC_DisableIRQ(EXTI9_5_IRQn);
            } else {
                if (!anyActive(10, 15)) NVIC_DisableIRQ(EXTI15_10_IRQn);
            }

            interrupt_callbacks[_pin] = {};
        }
    }
}
void GPIO::setMode(Mode mode) {
    if (mode == Mode::None) return;
    port()->MODER &= ~(0b11u << _pin * 2);
    port()->MODER |=  (static_cast<uint32_t>(mode) << _pin * 2);
}

void GPIO::setOutputType(OutputType otype) {
    if (otype == OutputType::None) return;
    port()->OTYPER &= ~(1u << _pin);
    port()->OTYPER |=  (static_cast<uint32_t>(otype) << _pin);
}

void GPIO::setSpeed(Speed speed) {
    if (speed == Speed::None) return;
    port()->OSPEEDR &= ~(0b11u << _pin * 2);
    port()->OSPEEDR |=  (static_cast<uint32_t>(speed) << _pin * 2);
}

void GPIO::setPull(Pull pull) {
    if (pull == Pull::None) return;
    port()->PUPDR &= ~(0b11u << _pin * 2);
    port()->PUPDR |=  (static_cast<uint32_t>(pull) << _pin * 2);
}

void GPIO::setAlternateFunction(AlternateFunction af) {
    if (af == AlternateFunction::None) return;
    if (_pin <= 7) {
        port()->AFR[0] &= ~(0b1111u << _pin * 4);
        port()->AFR[0] |=  (static_cast<uint32_t>(af) << _pin * 4);
    } else {
        port()->AFR[1] &= ~(0b1111u << (_pin - 8) * 4);
        port()->AFR[1] |=  (static_cast<uint32_t>(af) << (_pin - 8) * 4);
    }
}

void GPIO::set() {
    port()->BSRR = (1u << _pin);
}

void GPIO::reset() {
    port()->BSRR = (1u << (_pin + 16));
}

void GPIO::toggle() {
    port()->ODR ^= (1u << _pin);
}

bool GPIO::get() {
    return (port()->IDR & (1u << _pin)) != 0;
}

GPIO_TypeDef* GPIO::port() const {
    return reinterpret_cast<GPIO_TypeDef*>(_portBase);
}

bool GPIO::setInterruptCallback(Edge edge, void (*fn)(void*), void* param){
    if(interrupt_callbacks[_pin].fn != nullptr){
        return false;
    }

    if(!(RCC->APB2ENR & RCC_APB2ENR_SYSCFGEN)){
        RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;
    }

    // SYSCFG setup
    uint32_t exticr_val;
    switch (_portBase) {
        case GPIOA_BASE: exticr_val = 0; break;
        case GPIOB_BASE: exticr_val = 1; break;
        case GPIOC_BASE: exticr_val = 2; break;
        case GPIOD_BASE: exticr_val = 3; break;
        case GPIOE_BASE: exticr_val = 4; break;
        default: return false;
    }
    uint32_t exticr_idx = _pin / 4;
    uint32_t shift      = (_pin % 4) * 4;
    SYSCFG->EXTICR[exticr_idx] &= ~(0xFu << shift);
    SYSCFG->EXTICR[exticr_idx] |=  (exticr_val << shift);

    // EXTI setup
    EXTI->IMR |= 1U<<_pin;
    if(edge == Edge::Fall){
        EXTI->RTSR &= ~(1U<<_pin);
        EXTI->FTSR |= 1U<<_pin;
    }
    else if(edge == Edge::Rise){
        EXTI->FTSR &= ~(1U<<_pin);
        EXTI->RTSR |= 1U<<_pin;
    }
    else{
        EXTI->FTSR |= 1U<<_pin;
        EXTI->RTSR |= 1U<<_pin;
    }

    // Clear pending before enabling NVIC Otherwise an old pending bit could immediately fire.
    EXTI->PR = (1U << _pin); 
    switch(_pin){
        case 0: NVIC_EnableIRQ(EXTI0_IRQn); break;
        case 1: NVIC_EnableIRQ(EXTI1_IRQn); break;
        case 2: NVIC_EnableIRQ(EXTI2_IRQn); break;
        case 3: NVIC_EnableIRQ(EXTI3_IRQn); break;
        case 4: NVIC_EnableIRQ(EXTI4_IRQn); break;
        case 5: NVIC_EnableIRQ(EXTI9_5_IRQn); break;
        case 6: NVIC_EnableIRQ(EXTI9_5_IRQn); break;
        case 7: NVIC_EnableIRQ(EXTI9_5_IRQn); break;
        case 8: NVIC_EnableIRQ(EXTI9_5_IRQn); break;
        case 9: NVIC_EnableIRQ(EXTI9_5_IRQn); break;
        case 10: NVIC_EnableIRQ(EXTI15_10_IRQn); break;
        case 11: NVIC_EnableIRQ(EXTI15_10_IRQn); break;
        case 12: NVIC_EnableIRQ(EXTI15_10_IRQn); break;
        case 13: NVIC_EnableIRQ(EXTI15_10_IRQn); break;
        case 14: NVIC_EnableIRQ(EXTI15_10_IRQn); break;
        case 15: NVIC_EnableIRQ(EXTI15_10_IRQn); break;
        default: return false;
    }

    interrupt_callbacks[_pin].fn = fn;
    interrupt_callbacks[_pin].ctx = param;
    return true;
}

int8_t GPIO::portToIdx(uint32_t portBase) {
    switch (portBase) {
        case GPIOA_BASE: return 0;
        case GPIOB_BASE: return 1;
        case GPIOC_BASE: return 2;
        case GPIOD_BASE: return 3;
        case GPIOE_BASE: return 4;
        default:         return -1;
    }
}

void GPIO::enableClock(uint32_t portBase) {
    switch (portBase) {
        case GPIOA_BASE: RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN; break;
        case GPIOB_BASE: RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN; break;
        case GPIOC_BASE: RCC->AHB1ENR |= RCC_AHB1ENR_GPIOCEN; break;
        case GPIOD_BASE: RCC->AHB1ENR |= RCC_AHB1ENR_GPIODEN; break;
        case GPIOE_BASE: RCC->AHB1ENR |= RCC_AHB1ENR_GPIOEEN; break;
    }
}

extern "C" void EXTI_Handler(void){
    uint32_t pending = EXTI->PR;
    for (int pin = 0; pin < 16; pin++) {
        if (pending & (1U << pin)) {
            EXTI->PR = (1U << pin);
            GPIO::interrupt_callbacks[pin].invoke();
        }
    }
}
