#include <sys/stat.h>
#include <errno.h>
#include "uart.hpp"

extern "C"{
int _close(int fd){
    (void)fd;
    return -1;
}

int _getpid(void){
    return 1;
}

int _kill(int pid, int sig){
    (void)pid;
    (void)sig;
    errno = EINVAL;
    return -1;
}

int _lseek(int fd, int ptr, int dir){
    (void)fd;
    (void)ptr;
    (void)dir;
    return 0;
}

int _read(int fd, char *ptr, int len){
    (void)fd;
    (void)ptr;
    (void)len;
    return 0;
}

int _write(int fd, char *ptr, int len){
    UART::write_syscall(fd, ptr, len);
    return len;
}
}
