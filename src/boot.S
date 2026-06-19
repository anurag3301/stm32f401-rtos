    .section .vectors
vector_table:
    .word __stack
    .word reset_handler
    .word fault_handler     // NMI
    .word fault_handler     // HardFault
    .word fault_handler     // MemManage
    .word fault_handler     // BusFault
    .word fault_handler     // UsageFault
    .word default_handler   // Reserved
    .word default_handler   // Reserved
    .word default_handler   // Reserved
    .word default_handler   // Reserved
    .word vPortSVCHandler
    .word default_handler
    .word default_handler
    .word xPortPendSVHandler
    .word xPortSysTickHandler
      /* External Interrupts */
    .word WWDG_IRQHandler
    .word PVD_IRQHandler
    .word TAMP_STAMP_IRQHandler
    .word RTC_WKUP_IRQHandler
    .word FLASH_IRQHandler
    .word RCC_IRQHandler
    .word EXTI_Handler
    .word EXTI_Handler
    .word EXTI_Handler
    .word EXTI_Handler
    .word EXTI_Handler
    .word DMA1_Stream0_IRQHandler
    .word DMA1_Stream1_IRQHandler
    .word DMA1_Stream2_IRQHandler
    .word DMA1_Stream3_IRQHandler
    .word DMA1_Stream4_IRQHandler
    .word DMA1_Stream5_IRQHandler
    .word DMA1_Stream6_IRQHandler
    .word ADC_IRQHandler
    .zero 16
    .word EXTI_Handler
    .word TIM1_BRK_TIM9_IRQHandler
    .word TIM1_UP_TIM10_IRQHandler
    .word TIM1_TRG_COM_TIM11_IRQHandler
    .word TIM1_CC_IRQHandler
    .word TIM2_IRQHandler
    .word TIM3_IRQHandler
    .word TIM4_IRQHandler
    .word I2C1_EV_IRQHandler
    .word I2C1_ER_IRQHandler
    .word I2C2_EV_IRQHandler
    .word I2C2_ER_IRQHandler
    .word SPI1_IRQHandler
    .word SPI2_IRQHandler
    .word USART1_IRQHandler
    .word USART2_IRQHandler
    .zero 4                     // Reserved
    .word EXTI_Handler
    .word RTC_Alarm_IRQHandler
    .word OTG_FS_WKUP_IRQHandler
    .zero 16                    // Reserved
    .word DMA1_Stream7_IRQHandler
    .zero 4                    // Reserved
    .word SDIO_IRQHandler
    .word TIM5_IRQHandler
    .word SPI3_IRQHandler
    .zero 20                    // Reserved
    .word DMA2_Stream0_IRQHandler
    .word DMA2_Stream1_IRQHandler
    .word DMA2_Stream2_IRQHandler
    .word DMA2_Stream3_IRQHandler
    .word DMA2_Stream4_IRQHandler
    .zero 24                    // Reserved
    .word OTG_FS_IRQHandler
    .word DMA2_Stream5_IRQHandler
    .word DMA2_Stream6_IRQHandler
    .word DMA2_Stream7_IRQHandler
    .word USART6_IRQHandler
    .word I2C3_EV_IRQHandler
    .word I2C3_ER_IRQHandler
    .zero 28                    // Reserved
    .word FPU_IRQHandler
    .zero 8                    // Reserved
    .word SPI4_IRQHandler


    .section .text
    .align 1
    .type reset_handler, %function
reset_handler:
    bl start
    b .

    .section .text
    .align 1
    .type reset_handler, %function
default_handler:
    b .

.macro DEF_IRQ name
    .weak \name
    .thumb_set \name, default_handler
.endm

DEF_IRQ WWDG_IRQHandler
DEF_IRQ PVD_IRQHandler
DEF_IRQ TAMP_STAMP_IRQHandler
DEF_IRQ RTC_WKUP_IRQHandler
DEF_IRQ FLASH_IRQHandler
DEF_IRQ RCC_IRQHandler
DEF_IRQ EXTI0_IRQHandler
DEF_IRQ EXTI1_IRQHandler
DEF_IRQ EXTI2_IRQHandler
DEF_IRQ EXTI3_IRQHandler
DEF_IRQ EXTI4_IRQHandler
DEF_IRQ EXTI_Handler
DEF_IRQ DMA1_Stream0_IRQHandler
DEF_IRQ DMA1_Stream1_IRQHandler
DEF_IRQ DMA1_Stream2_IRQHandler
DEF_IRQ DMA1_Stream3_IRQHandler
DEF_IRQ DMA1_Stream4_IRQHandler
DEF_IRQ DMA1_Stream5_IRQHandler
DEF_IRQ DMA1_Stream6_IRQHandler
DEF_IRQ ADC_IRQHandler
DEF_IRQ EXTI9_5_IRQHandler
DEF_IRQ TIM1_BRK_TIM9_IRQHandler
DEF_IRQ TIM1_UP_TIM10_IRQHandler
DEF_IRQ TIM1_TRG_COM_TIM11_IRQHandler
DEF_IRQ TIM1_CC_IRQHandler
DEF_IRQ TIM2_IRQHandler
DEF_IRQ TIM3_IRQHandler
DEF_IRQ TIM4_IRQHandler
DEF_IRQ I2C1_EV_IRQHandler
DEF_IRQ I2C1_ER_IRQHandler
DEF_IRQ I2C2_EV_IRQHandler
DEF_IRQ I2C2_ER_IRQHandler
DEF_IRQ SPI1_IRQHandler
DEF_IRQ SPI2_IRQHandler
DEF_IRQ USART1_IRQHandler
DEF_IRQ USART2_IRQHandler
DEF_IRQ EXTI15_10_IRQHandler
DEF_IRQ RTC_Alarm_IRQHandler
DEF_IRQ OTG_FS_WKUP_IRQHandler
DEF_IRQ DMA1_Stream7_IRQHandler
DEF_IRQ SDIO_IRQHandler
DEF_IRQ TIM5_IRQHandler
DEF_IRQ SPI3_IRQHandler
DEF_IRQ DMA2_Stream0_IRQHandler
DEF_IRQ DMA2_Stream1_IRQHandler
DEF_IRQ DMA2_Stream2_IRQHandler
DEF_IRQ DMA2_Stream3_IRQHandler
DEF_IRQ DMA2_Stream4_IRQHandler
DEF_IRQ OTG_FS_IRQHandler
DEF_IRQ DMA2_Stream5_IRQHandler
DEF_IRQ DMA2_Stream6_IRQHandler
DEF_IRQ DMA2_Stream7_IRQHandler
DEF_IRQ USART6_IRQHandler
DEF_IRQ I2C3_EV_IRQHandler
DEF_IRQ I2C3_ER_IRQHandler
DEF_IRQ FPU_IRQHandler
DEF_IRQ SPI4_IRQHandler
