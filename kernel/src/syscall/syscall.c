#include <stdbool.h>
#include <stdint.h>

#include "minemu/kprintf.h"
#include "minemu/platform.h"
#include "minemu/syscall.h"
#include "minemu/trace.h"
#include "minemu/trap.h"
#include "minemu/uart.h"
#include "minemu/user_abi.h"
#include "minemu/uspace.h"

#define CPSR_MODE_MASK UINT32_C(0x1f)
#define CPSR_MODE_USR UINT32_C(0x10)
#define CPSR_THUMB UINT32_C(0x20)
#define A32_SVC_ZERO UINT32_C(0xef000000) /* "svc #0", condition AL */

#define IOCTL_ERROR MINEMU_IOCTL_ERROR

enum device { DEV_NONE, DEV_UART0, DEV_UART1, DEV_RNG, DEV_TRACE };

enum action {
    ACT_NONE,
    ACT_UART_READ,
    ACT_UART_WRITE,
    ACT_UART_STATUS,
    ACT_RNG_SEED,
    ACT_RNG_NEXT,
    ACT_RNG_STATE,
    ACT_TRACE_EVENT,
};

/*
 * fd -> device. Both numbering schemes from user_abi.h are accepted: the
 * small descriptors (0-4) and the handout's device base addresses.
 */
static enum device fd_device(int32_t fd) {
    switch ((uint32_t)fd) {
    case MINEMU_FD_UART_IN:
    case MINEMU_FD_UART_OUT:
    case MINEMU_IOCTL_FD_UART0:
        return DEV_UART0;
    case MINEMU_FD_UART_DIAG:
    case MINEMU_IOCTL_FD_UART1:
        return DEV_UART1;
    case MINEMU_FD_RNG:
    case MINEMU_IOCTL_FD_RNG:
        return DEV_RNG;
    case MINEMU_FD_TRACE:
    case MINEMU_IOCTL_FD_TRACE:
        return DEV_TRACE;
    default:
        return DEV_NONE;
    }
}

/* (device, op) -> action, accepting the handout op index or the request code. */
static enum action op_action(enum device device, uint32_t op) {
    switch (device) {
    case DEV_UART0:
    case DEV_UART1:
        if (op == MINEMU_IOCTL_OP_UART_READ || op == MINEMU_IOCTL_UART_READ) {
            return ACT_UART_READ;
        }
        if (op == MINEMU_IOCTL_OP_UART_WRITE || op == MINEMU_IOCTL_UART_WRITE) {
            return ACT_UART_WRITE;
        }
        return op == MINEMU_IOCTL_UART_STATUS ? ACT_UART_STATUS : ACT_NONE;
    case DEV_RNG:
        if (op == MINEMU_IOCTL_OP_RNG_SET_SEED || op == MINEMU_IOCTL_RNG_SEED) {
            return ACT_RNG_SEED;
        }
        if (op == MINEMU_IOCTL_OP_RNG_GET_DATA || op == MINEMU_IOCTL_RNG_NEXT) {
            return ACT_RNG_NEXT;
        }
        return op == MINEMU_IOCTL_RNG_STATE ? ACT_RNG_STATE : ACT_NONE;
    case DEV_TRACE:
        return (op == MINEMU_IOCTL_OP_TRACE_EVENT || op == MINEMU_IOCTL_TRACE_EVENT)
                   ? ACT_TRACE_EVENT
                   : ACT_NONE;
    default:
        return ACT_NONE;
    }
}

/*
 * Copy up to `count` already-received bytes into user memory at `dst`.
 * Never waits: returns 0 if nothing has arrived. The whole destination
 * range is checked before any byte leaves the UART, so a bad pointer
 * cannot swallow input.
 */
static int32_t uart_read(enum device device, uint32_t dst, int32_t count) {
    if (count < 0 || !uspace_writable(dst, (uint32_t)count)) {
        return IOCTL_ERROR;
    }
    if (device == DEV_UART0) {
        /* IRQs are masked here; pull in bytes the RX IRQ has not taken yet. */
        uart_rx_isr();
    }

    int32_t copied = 0;
    while (copied < count) {
        int c = device == DEV_UART0 ? uart_rx_pop() : uart1_try_getc();
        if (c < 0) {
            break;
        }
        char byte = (char)c;
        (void)uspace_copy_to(dst + (uint32_t)copied, &byte, 1);
        ++copied;
    }
    return copied;
}

/* Transmit `count` bytes from user memory at `src`, waiting on TX_READY. */
static int32_t uart_write(enum device device, uint32_t src, int32_t count) {
    if (count < 0 || !uspace_readable(src, (uint32_t)count)) {
        return IOCTL_ERROR;
    }
    for (int32_t i = 0; i < count; ++i) {
        char byte;
        (void)uspace_copy_from(&byte, src + (uint32_t)i, 1);
        if (device == DEV_UART0) {
            uart_putc(byte);
        } else {
            uart1_putc(byte);
        }
    }
    return count;
}

static int32_t uart_status(enum device device) {
    volatile struct minemu_uart_regs *uart = device == DEV_UART0 ? MINEMU_UART0 : MINEMU_UART1;
    uint32_t status = uart->status;
    int32_t result = 0;
    /* UART0 input may already sit in the kernel buffer rather than the device. */
    if ((status & MINEMU_UART_STATUS_RX_READY) != 0 ||
        (device == DEV_UART0 && uart_rx_count() != 0)) {
        result |= MINEMU_IOCTL_UART_RX_READY;
    }
    if ((status & MINEMU_UART_STATUS_TX_READY) != 0) {
        result |= MINEMU_IOCTL_UART_TX_READY;
    }
    return result;
}

/* Store one RNG register into *dst. The pointer is checked before the read,
 * because reading DATA advances the generator. */
static int32_t rng_read(uint32_t dst, bool next) {
    if (!uspace_writable(dst, sizeof(uint32_t))) {
        return IOCTL_ERROR;
    }
    uint32_t value = next ? MINEMU_RNG->data : MINEMU_RNG->state;
    (void)uspace_copy_to(dst, &value, sizeof(value));
    return 0;
}

/*
 * ioctl(fd, op, arg0, arg1). The variadic arguments arrive raw in r2/r3;
 * which of them are meaningful depends on the (device, op) pair.
 */
static int32_t sys_ioctl(int32_t fd, uint32_t op, uint32_t arg0, uint32_t arg1) {
    enum device device = fd_device(fd);
    switch (op_action(device, op)) {
    case ACT_UART_READ:
        return uart_read(device, arg0, (int32_t)arg1);
    case ACT_UART_WRITE:
        return uart_write(device, arg0, (int32_t)arg1);
    case ACT_UART_STATUS:
        return uart_status(device);
    case ACT_RNG_SEED:
        MINEMU_RNG->seed = arg0;
        return 0;
    case ACT_RNG_NEXT:
        return rng_read(arg0, true);
    case ACT_RNG_STATE:
        return rng_read(arg0, false);
    case ACT_TRACE_EVENT:
        minemu_trace_event(arg0);
        return 0;
    default:
        return IOCTL_ERROR; /* unknown fd, or an op that device lacks */
    }
}

/*
 * The process has nothing to return to: there is one user program and no
 * scheduler yet. Park the CPU with IRQs masked.
 */
static void __attribute__((noreturn)) sys_exit(int32_t status) {
    (void)status;
    minemu_fail_stop();
}

/*
 * C side of the syscall path, called by minemu_svc_trampoline with the saved
 * user state. Returns the frame to resume (always the same one for now).
 *
 * Only a genuine A32 "svc #0" from USR mode is a syscall; anything else gets
 * -1 in r0 and no effect. The syscall number is in r7, arguments in r0-r6,
 * and the result goes back in r0.
 */
struct minemu_trap_frame *minemu_svc_dispatch(struct minemu_trap_frame *frame) {
    uint32_t instruction = 0;
    if (frame->exception_id != MINEMU_EXCEPTION_SVC ||
        (frame->spsr & CPSR_MODE_MASK) != CPSR_MODE_USR || (frame->spsr & CPSR_THUMB) != 0 ||
        !uspace_copy_from(&instruction, frame->fault_pc, sizeof(instruction)) ||
        instruction != A32_SVC_ZERO) {
        frame->r[0] = (uint32_t)IOCTL_ERROR;
        return frame;
    }

    switch (frame->r[7]) {
    case MINEMU_SYSCALL_IOCTL:
        frame->r[0] = (uint32_t)sys_ioctl((int32_t)frame->r[0], frame->r[1], frame->r[2],
                                          frame->r[3]);
        break;
    case MINEMU_SYSCALL_EXIT:
        sys_exit((int32_t)frame->r[0]);
    default:
        frame->r[0] = (uint32_t)IOCTL_ERROR; /* not implemented */
        break;
    }
    return frame;
}

/* A user program that faults cannot be resumed; report it and stop. */
void minemu_undefined_dispatch(struct minemu_trap_frame *frame) {
    kprintf("undefined instruction at %p\n", (void *)(uintptr_t)frame->fault_pc);
    minemu_fail_stop();
}

void minemu_abort_dispatch(struct minemu_trap_frame *frame) {
    kprintf("%s abort at %p (fsr %x, far %p)\n",
            frame->exception_id == MINEMU_EXCEPTION_DATA_ABORT ? "data" : "prefetch",
            (void *)(uintptr_t)frame->fault_pc, (unsigned)frame->dfsr,
            (void *)(uintptr_t)frame->dfar);
    minemu_fail_stop();
}
