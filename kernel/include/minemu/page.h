#ifndef MINEMU_PAGE_H
#define MINEMU_PAGE_H

#include <stdint.h>

/*
 * Physical page allocator. Hands out 4 KiB pages of RAM above the kernel
 * image, in order, and never frees them (enough while there is one user
 * program). Pages are returned as kernel direct-map addresses and are
 * already zeroed.
 *
 * Pages come from here instead of static arrays because the bootloader
 * clears kernel BSS a byte at a time, so large static arrays slow down
 * every boot.
 */
void *page_alloc(void);

/* Physical address of a kernel direct-map address, and back. */
uint32_t page_kernel_to_paddr(const void *address);
void *page_paddr_to_kernel(uint32_t paddr);

#endif
