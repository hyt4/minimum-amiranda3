#ifndef MINEMU_MSH_H
#define MINEMU_MSH_H

/*
 * msh: the kernel-mode console shell on UART0.
 * Prints "msh> ", reads a line from the UART receive buffer, runs it, repeats.
 * Requires the UART0 RX interrupt to be set up and CPU IRQs enabled.
 */
void msh_run(void) __attribute__((noreturn));

#endif
