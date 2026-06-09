#ifndef __HAL_COMMON__
#define __HAL_COMMON__

struct Callback{
    void (*fn)(void*) = nullptr;
    void* ctx = nullptr;
    void invoke() const {if(fn) fn(ctx);}
};

#endif // __HAL_COMMON__
