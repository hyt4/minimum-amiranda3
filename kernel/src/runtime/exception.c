#include "minemu/irq.h"
#include "minemu/syscall.h"

void minemu_fail_stop(void) {
    for (;;) {
        __asm__ volatile("nop");
    }
}

void minemu_panic(const char *message) {
    (void)message;
    minemu_fail_stop();
}

__attribute__((weak, noreturn)) void minemu_svc_trampoline(void) {
    minemu_fail_stop();
}

/*
 * Weak IRQ placeholders. The kernel links its real trampoline and dispatcher
 * (src/irq/) as direct objects, which override these. They stay here because
 * the examples link this archive too, and an example without IRQ code still
 * needs the symbol that vectors.S branches to.
 */
__attribute__((weak, noreturn)) void minemu_irq_trampoline(void) {
    minemu_fail_stop();
}

__attribute__((weak)) struct minemu_trap_frame *minemu_svc_dispatch(
    struct minemu_trap_frame *frame) {
    (void)frame;
    minemu_fail_stop();
}

__attribute__((weak)) struct minemu_trap_frame *minemu_irq_dispatch(
    struct minemu_trap_frame *frame) {
    (void)frame;
    minemu_fail_stop();
}

__attribute__((weak)) void minemu_undefined_dispatch(struct minemu_trap_frame *frame) {
    (void)frame;
    minemu_fail_stop();
}

__attribute__((weak)) void minemu_abort_dispatch(struct minemu_trap_frame *frame) {
    (void)frame;
    minemu_fail_stop();
}
