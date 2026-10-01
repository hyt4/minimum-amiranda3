#ifndef MINEMU_KPRINTF_H
#define MINEMU_KPRINTF_H

/*
 * Minimal kernel printf that writes to the console UART via uart_putc().
 *
 * Supported conversions (no flags, width, precision, or length modifiers):
 *   %s  NUL-terminated string ("(null)" for a NULL pointer)
 *   %c  single character
 *   %d  signed decimal int
 *   %u  unsigned decimal int
 *   %x  unsigned hexadecimal int, lowercase, no leading zeros
 *   %p  pointer as "0x" followed by 8 hex digits
 *   %%  literal '%'
 * An unknown conversion is printed as-is (e.g. "%q") so mistakes stay visible.
 */
void kprintf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#endif
