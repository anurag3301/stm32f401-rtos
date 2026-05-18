    .section .vectors
vector_table:
    .word __stack
    .word reset_handler
    .zero 400

    .section .text
    .align 1
    .type reset_handler, %function
reset_handler:
    mov r1, #1
    mov r2, #2
    add r3, r1, r2
    b .
