#ifndef MINEMU_USPACE_H
#define MINEMU_USPACE_H

#include <stdbool.h>
#include <stdint.h>

#include "minemu/boot.h"

/*
 * The single user address space.
 *
 * Layout (one page table, page-directory slot 1, covering 0x00400000-0x007fffff):
 *   0x00400000 ...            module segments, permissions from the image
 *   USPACE_STACK_BOTTOM ...   user stack, RW and never executable
 *   USPACE_STACK_TOP          initial user SP (exclusive)
 * Everything else, kernel memory and MMIO included, stays supervisor-only.
 */
#define USPACE_IMAGE_BASE UINT32_C(0x00400000)
#define USPACE_STACK_PAGES 4U
#define USPACE_STACK_TOP UINT32_C(0x00800000)
#define USPACE_STACK_BOTTOM (USPACE_STACK_TOP - USPACE_STACK_PAGES * UINT32_C(4096))

/* Map the system image read-only into the kernel so modules can be read. */
bool uspace_map_image(const struct minemu_boot_info *boot_info);

/*
 * Find the module called `name`, copy its segments into fresh pages, zero
 * its BSS, map a stack, and install the page table. Returns the module's
 * entry address, or 0 on any malformed record.
 */
uint32_t uspace_load(const struct minemu_boot_info *boot_info, const char *name);

/* Drop to USR mode at `entry` with SP = `stack_top`. IRQs are enabled in USR. */
void uspace_enter(uint32_t entry, uint32_t stack_top) __attribute__((noreturn));

/*
 * User-pointer checks. A range is accessible only if every page it touches
 * is mapped with the USER bit and the needed permission. A zero-length
 * range is always accessible; a range that wraps past 0xffffffff never is.
 */
bool uspace_readable(uint32_t vaddr, uint32_t length);
bool uspace_writable(uint32_t vaddr, uint32_t length);

/*
 * Copy between kernel buffers and user memory. These re-check the range and
 * return false without copying anything if it is not accessible, so a caller
 * that validated first can still rely on them as a backstop.
 */
bool uspace_copy_from(void *dst, uint32_t user_src, uint32_t length);
bool uspace_copy_to(uint32_t user_dst, const void *src, uint32_t length);

#endif
