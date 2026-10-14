#include <stdint.h>

#include "minemu/user_abi.h"

/*
 * Exercises ioctl error handling and the handout's device-address form.
 * Each check traces its index, then 1 for the expected result or 0 if not,
 * so a failure shows up as a mismatch at a known position in trace_values.
 */

static const uint32_t read_only_word = 0;

static void check(uint32_t index, int ok)
{
  (void)ioctl(MINEMU_FD_TRACE, MINEMU_IOCTL_TRACE_EVENT, index);
  (void)ioctl(MINEMU_FD_TRACE, MINEMU_IOCTL_TRACE_EVENT, ok ? 1U : 0U);
}

void minemu_user_main(void)
{
  static char ok_text[] = "ok\n";
  static char uart1_text[] = "u1\n";
  uint32_t value = 0;

  /* 1-3: pointers outside the user's own memory are rejected. */
  check(1, ioctl(MINEMU_FD_UART_OUT, MINEMU_IOCTL_UART_WRITE,
                 (void *)0xc0030000U, 4U) < 0);
  check(2, ioctl(MINEMU_FD_UART_OUT, MINEMU_IOCTL_UART_WRITE, (void *)0, 1U) < 0);
  check(3, ioctl(MINEMU_FD_UART_OUT, MINEMU_IOCTL_UART_WRITE,
                 (void *)0x10004000U, 4U) < 0);

  /* 4: read-only destination is rejected, and the RNG does not advance. */
  check(4, ioctl(MINEMU_FD_RNG, MINEMU_IOCTL_RNG_NEXT, (uint32_t *)&read_only_word) < 0 &&
           ioctl(MINEMU_FD_RNG, MINEMU_IOCTL_RNG_STATE, &value) == 0 &&
           value == 0x4d454d55U);

  /* 5: a range that wraps past the top of memory is rejected. */
  check(5, ioctl(MINEMU_FD_UART_IN, MINEMU_IOCTL_UART_READ,
                 (void *)0xfffffff0U, 0x20U) < 0);

  /* 6-7: unknown fd and unknown op. */
  check(6, ioctl(99, MINEMU_IOCTL_UART_WRITE, ok_text, 3U) < 0);
  check(7, ioctl(MINEMU_FD_TRACE, MINEMU_IOCTL_UART_WRITE, ok_text, 3U) < 0);

  /* 8: zero-length write succeeds with nothing to do. */
  check(8, ioctl(MINEMU_FD_UART_OUT, MINEMU_IOCTL_UART_WRITE, (void *)0, 0U) == 0);

  /* 9: no input pending, so a read returns 0 bytes. */
  check(9, ioctl(MINEMU_FD_UART_IN, MINEMU_IOCTL_UART_READ, ok_text, 3U) == 0);

  /* 10-12: handout form (device-address fd, small op numbers). */
  check(10, ioctl(MINEMU_IOCTL_FD_UART0, MINEMU_IOCTL_OP_UART_WRITE, ok_text, 3U) == 3);
  check(11, ioctl(MINEMU_IOCTL_FD_RNG, MINEMU_IOCTL_OP_RNG_SET_SEED, 0x12345678U) == 0 &&
            ioctl(MINEMU_FD_RNG, MINEMU_IOCTL_RNG_STATE, &value) == 0 &&
            value == 0x12345678U);
  check(12, ioctl(MINEMU_IOCTL_FD_RNG, MINEMU_IOCTL_OP_RNG_GET_DATA, &value) == 0 &&
            value == 0x87985aa5U);

  /* 13: UART1 accepts writes. */
  check(13, ioctl(MINEMU_IOCTL_FD_UART1, MINEMU_IOCTL_OP_UART_WRITE, uart1_text, 3U) == 3);

  /* Handout-form trace event. */
  (void)ioctl(MINEMU_IOCTL_FD_TRACE, MINEMU_IOCTL_OP_TRACE_EVENT, 0x600dU);

  __asm__ volatile("bkpt #0");
  while (1);
}
