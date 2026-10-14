#include "minemu/page.h"

#include <stddef.h>

#include "minemu/mmu.h"
#include "minemu/platform.h"

extern uint8_t __minemu_kernel_end[];

/* Kernel VA of the next page to hand out; 0 until the first call. */
static uintptr_t next_page;

void *page_alloc(void) {
    if (next_page == 0) {
        next_page = (uintptr_t)__minemu_kernel_end;
    }
    if (next_page >= MINEMU_KERNEL_DIRECT_BASE + MINEMU_KERNEL_DIRECT_SIZE) {
        return NULL;
    }

    uint32_t *page = (uint32_t *)next_page;
    next_page += MINEMU_PAGE_SIZE;
    for (size_t i = 0; i < MINEMU_PAGE_SIZE / sizeof(uint32_t); ++i) {
        page[i] = 0;
    }
    return page;
}

uint32_t page_kernel_to_paddr(const void *address) {
    return (uint32_t)(uintptr_t)address - MINEMU_KERNEL_DIRECT_BASE + MINEMU_RAM_BASE;
}

void *page_paddr_to_kernel(uint32_t paddr) {
    return (void *)(uintptr_t)(paddr - MINEMU_RAM_BASE + MINEMU_KERNEL_DIRECT_BASE);
}
