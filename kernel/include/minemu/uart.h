#ifndef MINEMU_UART_H
#define MINEMU_UART_H

/*
 * Driver for the console UART (UART0).
 *
 * Transmit is polled: uart_putc() busy-waits on TX_READY.
 * Receive is interrupt-driven: uart_rx_isr() moves bytes from the device into
 * a ring buffer, and uart_getc()/uart_try_getc() read from that buffer, never
 * from the device directly.
 *
 * Register access rules (violations data-abort in minemu):
 *   rx_data  read-only   one received byte in bits [7:0]
 *   tx_data  write-only  one byte to transmit in bits [7:0]
 *   status   read-only   MINEMU_UART_STATUS_RX_READY / _TX_READY
 *   control  read/write  only MINEMU_UART_CONTROL_RX_IRQ_ENABLE is defined
 * Every access is a single aligned 32-bit load or store through MINEMU_UART0.
 *
 * Bring-up order (see minemu_kernel_main):
 *   uart_init(); irq_register_handler(MINEMU_IRQ_UART0, uart_rx_isr);
 *   uart_enable_rx_irq(); irq_enable_source(MINEMU_IRQ_UART0);
 *   minemu_irq_enable();
 */

/* Empty the receive buffer and leave the UART in polled mode (RX IRQ off). */
void uart_init(void);

/* Make the UART raise its interrupt level whenever RX_READY is set. */
void uart_enable_rx_irq(void);

/* Wait until the transmitter is ready, then send one byte. */
void uart_putc(char c);

/* Send a NUL-terminated string, byte for byte (no newline translation). */
void uart_puts(const char *s);

/*
 * Return the next buffered byte (0-255), or -1 if the buffer is empty.
 * Never blocks. Call with CPU IRQs enabled: it masks them briefly and
 * unconditionally re-enables them.
 */
int uart_try_getc(void);

/* Wait until a byte has been buffered, then return it. Call with IRQs enabled. */
char uart_getc(void);

/* UART0 RX interrupt handler: drain the device into the buffer. */
void uart_rx_isr(void);

#endif
