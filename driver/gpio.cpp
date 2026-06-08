#include "gpio.hpp"

bool GPIO::inuse[5][16] = {};

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

GPIO::~GPIO() {
    if (_inuseIdx != -1) inuse[_inuseIdx][_pin] = false;
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
