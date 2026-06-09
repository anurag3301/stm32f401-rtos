#ifndef _CONFIG_H__
#define _CONFIG_H__

#define configTICK_TYPE_WIDTH_IN_BITS           TICK_TYPE_WIDTH_32_BITS
#define configMINIMAL_STACK_SIZE                0x100
#define configMAX_PRIORITIES                    15
#define configUSE_PREEMPTION                    1 
#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0
#define configTOTAL_HEAP_SIZE                   0x1000 
#define configCPU_CLOCK_HZ                      84000000 
#define configTICK_RATE_HZ                      10
#define INCLUDE_vTaskDelay                      1
#define INCLUDE_vTaskDelete                     1

#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY    5
#define configMAX_SYSCALL_INTERRUPT_PRIORITY            ( configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - 4) )

#endif
