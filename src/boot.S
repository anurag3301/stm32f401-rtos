    .section .vectors
vector_table:
    .word __stack
    .word reset_handler
    .word default
    .word default
    .word default
    .word default
    .word default
    .word default
    .word default
    .word default
    .word default
    .word vPortSVCHandler
    .word default
    .word default
    .word xPortPendSVHandler
    .word xPortSysTickHandler
    .zero 400

    .section .text
    .align 1
    .type reset_handler, %function
reset_handler:
    bl start
    b .

    .section .text
    .align 1
    .type reset_handler, %function
default:
    b .
