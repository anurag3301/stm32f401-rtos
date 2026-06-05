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

enum class AlternateFunction: uint8_t{
    AF0, AF1, AF2, AF3, AF4, AF5, 
    AF6, AF7, AF8, AF9, AF10, AF11, 
    AF12, AF13, AF14, AF15, NONE
};

int8_t portToIdx(uint32_t portBase);
void enableClock(uint32_t portBase);
extern bool inuse[5][16];

template<uint32_t PortBase, uint8_t PinNo>
class Pin{
public:
    Pin(Mode mode, OutputType otype, Speed speed, 
        Pull pull, AlternateFunction af = AlternateFunction::NONE)
    {
        inuseIdx = portToIdx(PortBase);
        if(inuseIdx == -1 || inuse[inuseIdx][PinNo]){
            return;
        }
        enableClock(PortBase);
        setMode(mode);
        setOutputType(otype);
        setSpeed(speed);
        setPull(pull);
        setAlternateFunction(af);
        inuse[inuseIdx][PinNo] = true;
    }

    ~Pin(){
        inuse[inuseIdx][PinNo] = false;
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

    void setAlternateFunction(AlternateFunction af){
        if(af == AlternateFunction::NONE) return;
        if(PinNo <= 7){
            port()->AFR[0] &= ~(0b1111u << PinNo*4);
            port()->AFR[0] |= (static_cast<uint32_t>(af) << PinNo*4);
        }
        else{
            port()->AFR[1] &= ~(0b1111u << (PinNo-8)*4);
            port()->AFR[1] |= (static_cast<uint32_t>(af) << (PinNo-8)*4);
        }
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

    bool get(){
        return (port()->IDR & (1u << PinNo) != 0);
    }

private:
    int8_t inuseIdx;
    static GPIO_TypeDef* port(){
        return reinterpret_cast<GPIO_TypeDef*>(PortBase);
    }

};

}

#endif
