#include "minemu/irq.h"

#include <stddef.h>

#include "minemu/kirq.h"
#include "minemu/platform.h"

/* Handler table indexed by controller source ID; NULL means unregistered. */
static irq_handler_t handlers[IRQ_SOURCE_COUNT];

/*
 * Software copy of the controller's ENABLE register, so enabling or disabling
 * one source never needs a read-modify-write of the device register.
 */
static uint32_t enabled_sources;

void irq_init(void) {
    for (uint32_t i = 0; i < IRQ_SOURCE_COUNT; ++i) {
        handlers[i] = NULL;
    }
    enabled_sources = 0;
    MINEMU_INTERRUPT->enable = 0;
}

int irq_register_handler(uint32_t source, irq_handler_t handler) {
    if (source >= IRQ_SOURCE_COUNT) {
        return -1;
    }
    handlers[source] = handler;
    return 0;
}

void irq_enable_source(uint32_t source) {
    if (source >= IRQ_SOURCE_COUNT) {
        return;
    }
    enabled_sources |= UINT32_C(1) << source;
    MINEMU_INTERRUPT->enable = enabled_sources & MINEMU_IRQ_ENABLE_MASK;
}

void irq_disable_source(uint32_t source) {
    if (source >= IRQ_SOURCE_COUNT) {
        return;
    }
    enabled_sources &= ~(UINT32_C(1) << source);
    MINEMU_INTERRUPT->enable = enabled_sources & MINEMU_IRQ_ENABLE_MASK;
}

struct minemu_trap_frame *minemu_irq_dispatch(struct minemu_trap_frame *frame) {
    /* The trampoline already read CLAIM and stored the source here. */
    uint32_t source = (uint32_t)frame->exception_id;

    /*
     * CLAIM returned no source (MINEMU_IRQ_NONE) or an ID the controller does
     * not define. There is no active claim, and EOI with no claim or a wrong
     * ID is an invalid write, so return without touching EOI.
     */
    if (source >= IRQ_SOURCE_COUNT) {
        return frame;
    }

    if (handlers[source] != NULL) {
        handlers[source]();
    } else {
        /*
         * Enabled but no handler: nothing will clear the device's level, so
         * the IRQ would re-fire forever after EOI. Mask the source instead.
         */
        irq_disable_source(source);
    }

    /* Release the claim so the controller can select a source again. */
    MINEMU_INTERRUPT->eoi = source;
    return frame;
}
