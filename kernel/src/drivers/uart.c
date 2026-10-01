#include "minemu/uart.h"

#include "minemu/irq.h"
#include "minemu/platform.h"

/*
 * Receive ring buffer, shared between uart_rx_isr (producer, IRQ mode) and
 * uart_try_getc (consumer, normal kernel code). The indices run freely and
 * are reduced modulo the power-of-two size when indexing; head - tail is the
 * number of buffered bytes.
 */
#define RX_BUFFER_SIZE 256u
_Static_assert((RX_BUFFER_SIZE & (RX_BUFFER_SIZE - 1)) == 0, "RX buffer size must be a power of two");

static char rx_buffer[RX_BUFFER_SIZE];
static uint32_t rx_head; /* next slot the ISR writes */
static uint32_t rx_tail; /* next slot the reader takes */

void uart_init(void) {
    rx_head = 0;
    rx_tail = 0;
    MINEMU_UART0->control = 0;
}

void uart_enable_rx_irq(void) {
    MINEMU_UART0->control = MINEMU_UART_CONTROL_RX_IRQ_ENABLE;
}

void uart_putc(char c) {
    /* Writing before TX_READY could lose the byte. */
    while ((MINEMU_UART0->status & MINEMU_UART_STATUS_TX_READY) == 0) {
    }
    /* Zero-extend so only bits [7:0] are ever set in the 32-bit store. */
    MINEMU_UART0->tx_data = (uint32_t)(uint8_t)c;
}

void uart_puts(const char *s) {
    while (*s != '\0') {
        uart_putc(*s++);
    }
}

void uart_rx_isr(void) {
    /*
     * Drain every byte the device holds. The UART's interrupt level is
     * RX_IRQ_ENABLE AND RX_READY, and EOI does not clear it, so leaving even
     * one byte behind would make the IRQ fire again right after EOI.
     */
    while ((MINEMU_UART0->status & MINEMU_UART_STATUS_RX_READY) != 0) {
        /* Reading RX_DATA consumes the byte; keep only the data bits. */
        char c = (char)(MINEMU_UART0->rx_data & UINT32_C(0xff));
        if (rx_head - rx_tail < RX_BUFFER_SIZE) {
            rx_buffer[rx_head % RX_BUFFER_SIZE] = c;
            ++rx_head;
        }
        /* Buffer full: drop the byte. It was still read, so RX_READY can clear. */
    }
}

int uart_try_getc(void) {
    int c = -1;

    /*
     * Mask IRQs while touching the shared buffer so uart_rx_isr cannot run in
     * the middle of our check-and-take and leave us with a stale view of
     * rx_head. The "memory" clobber in the irq.h helpers also stops the
     * compiler from caching rx_head/rx_tail across this critical section.
     */
    minemu_irq_disable();
    if (rx_tail != rx_head) {
        c = (int)(uint8_t)rx_buffer[rx_tail % RX_BUFFER_SIZE];
        ++rx_tail;
    }
    minemu_irq_enable();

    return c;
}

char uart_getc(void) {
    /*
     * Wait with IRQs enabled between polls, so the RX interrupt can run and
     * fill the buffer. This cannot miss a wakeup because it never sleeps:
     * every iteration re-checks the buffer. WFI is deliberately not used;
     * under minemu it does not wake on an IRQ that becomes pending while
     * masked, which is exactly the window a race-free WFI idle needs.
     */
    for (;;) {
        int c = uart_try_getc();
        if (c >= 0) {
            return (char)c;
        }
    }
}
