#ifndef MINEMU_KIRQ_H
#define MINEMU_KIRQ_H

#include <stdint.h>

/*
 * Kernel interrupt-controller layer. The CPU-side entry points
 * (minemu_irq_trampoline, minemu_irq_dispatch) are declared in minemu/irq.h.
 *
 * Flow of one interrupt:
 *   device raises its level -> controller selects and claims a source
 *   -> IRQ vector -> minemu_irq_trampoline (saves frame, reads CLAIM)
 *   -> minemu_irq_dispatch (calls handlers[source], writes EOI) -> resume.
 */

/* Number of controller sources: SysTick 0, UART0 1, UART1 2, block 3. */
#define IRQ_SOURCE_COUNT 4u

/* A device interrupt handler. Runs in IRQ mode with IRQs masked; it must clear
 * the device's interrupt condition before returning, or the IRQ re-fires. */
typedef void (*irq_handler_t)(void);

/* Mask every source and forget every handler. Call once, with CPU IRQs off. */
void irq_init(void);

/* Install the handler for one source. Returns 0, or -1 for an invalid source. */
int irq_register_handler(uint32_t source, irq_handler_t handler);

/* Unmask / mask one source at the controller. Invalid sources are ignored. */
void irq_enable_source(uint32_t source);
void irq_disable_source(uint32_t source);

#endif
