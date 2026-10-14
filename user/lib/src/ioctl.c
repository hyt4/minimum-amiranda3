#include <stdarg.h>
#include <stdint.h>

#include "minemu/user_abi.h"

static int32_t ioctl_svc(int fd, uint32_t request, uint32_t argument0,
                         uint32_t argument1) {
    register uint32_t r0 __asm__("r0") = (uint32_t)fd;
    register uint32_t r1 __asm__("r1") = request;
    register uint32_t r2 __asm__("r2") = argument0;
    register uint32_t r3 __asm__("r3") = argument1;
    register uint32_t r7 __asm__("r7") = MINEMU_SYSCALL_IOCTL;

    __asm__ volatile("svc #0"
                     : "+r"(r0)
                     : "r"(r1), "r"(r2), "r"(r3), "r"(r7)
                     : "memory", "cc");
    return (int32_t)r0;
}

/* Which variadic arguments a request takes. */
enum ioctl_args { ARGS_NONE, ARGS_BUFFER, ARGS_POINTER, ARGS_VALUE };

static enum ioctl_args ioctl_args(int fd, uint32_t request) {
    switch (request) {
    case MINEMU_IOCTL_UART_READ:
    case MINEMU_IOCTL_UART_WRITE:
        return ARGS_BUFFER;
    case MINEMU_IOCTL_RNG_NEXT:
    case MINEMU_IOCTL_RNG_STATE:
        return ARGS_POINTER;
    case MINEMU_IOCTL_RNG_SEED:
    case MINEMU_IOCTL_TRACE_EVENT:
        return ARGS_VALUE;
    default:
        break;
    }

    /* Device-address descriptors use small per-device op numbers. */
    switch ((uint32_t)fd) {
    case MINEMU_IOCTL_FD_UART0:
    case MINEMU_IOCTL_FD_UART1:
        return (request == MINEMU_IOCTL_OP_UART_READ ||
                request == MINEMU_IOCTL_OP_UART_WRITE)
                   ? ARGS_BUFFER
                   : ARGS_NONE;
    case MINEMU_IOCTL_FD_RNG:
        if (request == MINEMU_IOCTL_OP_RNG_SET_SEED) {
            return ARGS_VALUE;
        }
        return request == MINEMU_IOCTL_OP_RNG_GET_DATA ? ARGS_POINTER
                                                       : ARGS_NONE;
    case MINEMU_IOCTL_FD_TRACE:
        return request == MINEMU_IOCTL_OP_TRACE_EVENT ? ARGS_VALUE : ARGS_NONE;
    default:
        return ARGS_NONE;
    }
}

int32_t ioctl(int fd, uint32_t request, ...) {
    uint32_t argument0 = 0;
    uint32_t argument1 = 0;
    va_list arguments;

    va_start(arguments, request);
    switch (ioctl_args(fd, request)) {
    case ARGS_BUFFER:
        argument0 = (uint32_t)(uintptr_t)va_arg(arguments, void *);
        argument1 = va_arg(arguments, uint32_t);
        break;
    case ARGS_POINTER:
        argument0 = (uint32_t)(uintptr_t)va_arg(arguments, uint32_t *);
        break;
    case ARGS_VALUE:
        argument0 = va_arg(arguments, uint32_t);
        break;
    case ARGS_NONE:
        break;
    }
    va_end(arguments);
    return ioctl_svc(fd, request, argument0, argument1);
}
