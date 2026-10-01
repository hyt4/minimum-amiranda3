#include "minemu/kprintf.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include "minemu/uart.h"

static const char hex_digits[] = "0123456789abcdef";

/*
 * No integer division anywhere in this file. Cortex-A9 has no divide
 * instruction in A32 state, so gcc calls libgcc's __aeabi_uidiv for '/' and
 * '%', and this toolchain's v7-a libgcc is Thumb code, which minemu does not
 * execute. Decimal uses repeated subtraction and hex uses shifts instead.
 */

static const uint32_t powers_of_ten[] = {
    1000000000u, 100000000u, 10000000u, 1000000u, 100000u,
    10000u, 1000u, 100u, 10u, 1u,
};

/* Print value in decimal with no leading zeros. */
static void print_decimal(uint32_t value) {
    int started = 0;

    for (size_t i = 0; i < sizeof(powers_of_ten) / sizeof(powers_of_ten[0]); ++i) {
        /* Each digit is how many times this power fits: at most 9 (4 at 10^9). */
        uint32_t digit = 0;
        while (value >= powers_of_ten[i]) {
            value -= powers_of_ten[i];
            ++digit;
        }
        /* Skip leading zeros, but always print the final digit so 0 prints "0". */
        if (digit != 0 || started || powers_of_ten[i] == 1u) {
            uart_putc((char)('0' + digit));
            started = 1;
        }
    }
}

/* Print value in lowercase hex with no leading zeros. */
static void print_hex(uint32_t value) {
    int started = 0;

    for (int shift = 28; shift >= 0; shift -= 4) {
        uint32_t nibble = (value >> shift) & 0xfu;
        if (nibble != 0 || started || shift == 0) {
            uart_putc(hex_digits[nibble]);
            started = 1;
        }
    }
}

static void print_signed(int32_t value) {
    uint32_t magnitude = (uint32_t)value;

    if (value < 0) {
        uart_putc('-');
        /* Negate in unsigned arithmetic so INT32_MIN does not overflow. */
        magnitude = 0u - magnitude;
    }
    print_decimal(magnitude);
}

/* Always print all 8 digits so addresses line up and are unambiguous. */
static void print_pointer(uintptr_t value) {
    uart_puts("0x");
    for (int shift = 28; shift >= 0; shift -= 4) {
        uart_putc(hex_digits[(value >> shift) & 0xf]);
    }
}

void kprintf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);

    for (; *fmt != '\0'; ++fmt) {
        if (*fmt != '%') {
            uart_putc(*fmt);
            continue;
        }

        ++fmt; /* Step past '%' to the conversion character. */
        switch (*fmt) {
        case 's': {
            const char *s = va_arg(args, const char *);
            uart_puts(s != NULL ? s : "(null)");
            break;
        }
        case 'c':
            /* char is promoted to int when passed through "...". */
            uart_putc((char)va_arg(args, int));
            break;
        case 'd':
            print_signed(va_arg(args, int));
            break;
        case 'u':
            print_decimal(va_arg(args, unsigned int));
            break;
        case 'x':
            print_hex(va_arg(args, unsigned int));
            break;
        case 'p':
            print_pointer((uintptr_t)va_arg(args, void *));
            break;
        case '%':
            uart_putc('%');
            break;
        case '\0':
            /* Format ended with a lone '%': print it and stop. */
            uart_putc('%');
            va_end(args);
            return;
        default:
            uart_putc('%');
            uart_putc(*fmt);
            break;
        }
    }

    va_end(args);
}
