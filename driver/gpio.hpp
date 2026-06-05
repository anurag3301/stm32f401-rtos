#ifndef __GPIO_HAL__
#define __GPIO_HAL__
#include <stm32f4xx.h>

namespace GPIO{
enum class Mode : uint8_t {
    Input,
    Output,
    Alternate,
    Analog
};

enum class OutputType : uint8_t {
    PushPull,
    OpenDrain
};

enum class Speed : uint8_t {
    Low,
    Medium,
    High,
    VeryHigh
};

enum class Pull : uint8_t {
    NoPUD,
    PullUp,
    PullDown,
};

template<uint32_t PortBase, uint8_t PinNo>
class Pin{
public:
    Pin(Mode mode, OutputType otype, Speed speed, Pull pull){
        setMode(mode);
        setOutputType(otype);
        setSpeed(speed);
        setPull(pull);
    }
    
    void setMode(Mode mode){
        port()->MODER &= ~(0b11u << PinNo*2);
        port()->MODER |= (static_cast<uint32_t>(mode) << PinNo*2);
    }

    void setOutputType(OutputType otype){
        port()->OTYPER &= ~(1u << PinNo);
        port()->OTYPER |= (static_cast<uint32_t>(otype) << PinNo);
    }

    void setSpeed(Speed speed){
        port()->OSPEEDR &= ~(0b11u << PinNo*2);
        port()->OSPEEDR |= (static_cast<uint32_t>(speed) << PinNo*2);
    }

    void setPull(Pull pull){
        port()->PUPDR &= ~(0b11u << PinNo*2);
        port()->PUPDR |= (static_cast<uint32_t>(pull) << PinNo*2);
    }

    void set(){
        port()->BSRR = (1u << PinNo);
    }

    void reset(){
        port()->BSRR = (1u << (PinNo + 16));
    }

    void toggle(){
        port()->ODR ^= (1u << PinNo);
    }

private:
    static GPIO_TypeDef* port(){
        return reinterpret_cast<GPIO_TypeDef*>(PortBase);
    }
};

}

#endif
