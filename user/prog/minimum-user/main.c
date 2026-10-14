#include <stdint.h>

#include "minemu/user_abi.h"

/* HW2 task 1: print through the ioctl syscall from USR mode, then idle. */
void minemu_user_main(void) {
    static const char message[] = "hello world\n";
    (void)ioctl(MINEMU_FD_UART_OUT, MINEMU_IOCTL_UART_WRITE, (void *)message,
                sizeof(message) - 1U);
    for (;;) {
    }
}
