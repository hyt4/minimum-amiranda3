#include "minemu/uspace.h"

#include <stddef.h>

#include "minemu/mmu.h"
#include "minemu/page.h"
#include "minemu/platform.h"

/*
 * Loader and page-table code adapted from the course example
 * examples/user-mode-hello/kernel/main.c (map_system_rom, find_module,
 * load_user_module). The user-pointer checks are new for HW2.
 */

#define BOOTSTRAP_PAGE_DIRECTORY_PADDR UINT32_C(0x40010000)
#define SYSTEM_ROM_TABLE_COUNT 4U
#define PAGE_SIZE MINEMU_PAGE_SIZE
#define PAGE_MASK MINEMU_PTE_PAGE_MASK
#define USER_PTE_BITS (MINEMU_PTE_VALID | MINEMU_PTE_USER)

/*
 * The user page table. Every page behind it (and the table itself) comes
 * from page_alloc(), so it is zeroed: an unused entry is an invalid PTE.
 */
static uint32_t *user_page_table;

static bool bytes_equal(const uint8_t *a, const char *b, size_t length) {
    for (size_t i = 0; i < length; ++i) {
        if (a[i] != (uint8_t)b[i]) {
            return false;
        }
    }
    return true;
}

static size_t str_length(const char *s) {
    size_t n = 0;
    while (s[n] != '\0') {
        ++n;
    }
    return n;
}

#define kernel_to_paddr page_kernel_to_paddr
#define paddr_to_kernel page_paddr_to_kernel

static volatile uint32_t *page_directory(void) {
    return (volatile uint32_t *)paddr_to_kernel(BOOTSTRAP_PAGE_DIRECTORY_PADDR);
}

/* [offset, offset + length) lies inside the image (no overflow). */
static bool image_range_valid(uint32_t offset, uint32_t length, uint32_t image_size) {
    return offset <= image_size && length <= image_size - offset;
}

/* `count` 32-byte records starting at `offset` lie inside the image. */
static bool image_records_valid(uint32_t offset, uint32_t count, uint32_t image_size) {
    return offset <= image_size && count <= ((image_size - offset) >> 5);
}

bool uspace_map_image(const struct minemu_boot_info *boot_info) {
    if (boot_info->system_rom_base != MINEMU_SYSTEM_ROM_BASE || boot_info->image_size == 0 ||
        boot_info->image_size > MINEMU_SYSTEM_ROM_SIZE) {
        return false;
    }

    uint32_t page_count = (boot_info->image_size + PAGE_SIZE - 1U) / PAGE_SIZE;
    uint32_t table_count = (page_count + 1023U) / 1024U;
    if (table_count > SYSTEM_ROM_TABLE_COUNT) {
        return false;
    }

    /* Supervisor read-only: no USER, WRITABLE or EXECUTABLE bit. */
    volatile uint32_t *directory = page_directory();
    for (uint32_t table = 0; table < table_count; ++table) {
        uint32_t *entries_page = page_alloc();
        if (entries_page == NULL) {
            return false;
        }
        uint32_t remaining = page_count - table * 1024U;
        uint32_t entries = remaining < 1024U ? remaining : 1024U;
        for (uint32_t i = 0; i < entries; ++i) {
            uint32_t page = table * 1024U + i;
            entries_page[i] = (boot_info->system_rom_base + page * PAGE_SIZE) |
                                          MINEMU_PTE_VALID | MINEMU_PTE_READABLE;
        }
        directory[(boot_info->system_rom_base >> 22) + table] =
            kernel_to_paddr(entries_page) | MINEMU_PDE_VALID;
    }
    minemu_mmu_tlbiall();
    return true;
}

static const struct minemu_module_record *find_module(const struct minemu_boot_info *boot_info,
                                                       const char *name) {
    const uint8_t *image = (const uint8_t *)(uintptr_t)boot_info->system_rom_base;
    size_t name_length = str_length(name);
    if (!image_records_valid(boot_info->module_table_offset, boot_info->module_count,
                             boot_info->image_size)) {
        return NULL;
    }

    const struct minemu_module_record *modules =
        (const struct minemu_module_record *)(image + boot_info->module_table_offset);
    for (uint32_t i = 0; i < boot_info->module_count; ++i) {
        const struct minemu_module_record *module = &modules[i];
        if (module->name_length == name_length &&
            image_range_valid(module->name_offset, module->name_length, boot_info->image_size) &&
            bytes_equal(image + module->name_offset, name, name_length)) {
            return module;
        }
    }
    return NULL;
}

static uint32_t pte_flags_from_segment(uint32_t segment_flags) {
    uint32_t flags = 0;
    if ((segment_flags & MINEMU_SEGMENT_READABLE) != 0) {
        flags |= MINEMU_PTE_READABLE;
    }
    if ((segment_flags & MINEMU_SEGMENT_WRITABLE) != 0) {
        flags |= MINEMU_PTE_WRITABLE;
    }
    if ((segment_flags & MINEMU_SEGMENT_EXECUTABLE) != 0) {
        flags |= MINEMU_PTE_EXECUTABLE;
    }
    return flags;
}

/*
 * Back every page of [vaddr, vaddr + length) with a fresh zeroed page that
 * has permissions `flags`. Two segments may share a page only if they ask
 * for the same permissions.
 */
static bool allocate_range(uint32_t vaddr, uint32_t length, uint32_t flags) {
    uint32_t end = vaddr + length;
    for (uint32_t page = vaddr & PAGE_MASK; page < end; page += PAGE_SIZE) {
        uint32_t *pte = &user_page_table[(page >> 12) & 1023U];
        if (*pte != 0) {
            if ((*pte & ~PAGE_MASK) != (USER_PTE_BITS | flags)) {
                return false;
            }
            continue;
        }
        void *frame = page_alloc();
        if (frame == NULL) {
            return false;
        }
        *pte = kernel_to_paddr(frame) | USER_PTE_BITS | flags;
    }
    return true;
}

static uint8_t *user_alias(uint32_t vaddr);

/* Copy initialized bytes into pages allocate_range() already mapped. */
static void fill_range(uint32_t vaddr, const uint8_t *src, uint32_t length) {
    for (uint32_t i = 0; i < length; ++i) {
        *user_alias(vaddr + i) = src[i];
    }
}

static bool segment_valid(const struct minemu_module_segment *segment, uint32_t image_size) {
    const uint32_t known = MINEMU_SEGMENT_READABLE | MINEMU_SEGMENT_WRITABLE |
                           MINEMU_SEGMENT_EXECUTABLE;
    return segment->memory_size != 0 && segment->file_size <= segment->memory_size &&
           (segment->flags & ~known) == 0 &&
           /* executable implies readable */
           ((segment->flags & MINEMU_SEGMENT_EXECUTABLE) == 0 ||
            (segment->flags & MINEMU_SEGMENT_READABLE) != 0) &&
           /* inside the image window, below the stack */
           segment->virtual_address >= USPACE_IMAGE_BASE &&
           segment->virtual_address < USPACE_STACK_BOTTOM &&
           segment->memory_size <= USPACE_STACK_BOTTOM - segment->virtual_address &&
           image_range_valid(segment->data_offset, segment->file_size, image_size);
}

uint32_t uspace_load(const struct minemu_boot_info *boot_info, const char *name) {
    const struct minemu_module_record *module = find_module(boot_info, name);
    const uint8_t *image = (const uint8_t *)(uintptr_t)boot_info->system_rom_base;
    if (module == NULL || module->segment_count == 0 || (module->entry_vaddr & 3U) != 0 ||
        !image_records_valid(module->segment_table_offset, module->segment_count,
                             boot_info->image_size)) {
        return 0;
    }

    const struct minemu_module_segment *segments =
        (const struct minemu_module_segment *)(image + module->segment_table_offset);
    bool entry_is_executable = false;
    user_page_table = page_alloc();
    if (user_page_table == NULL) {
        return 0;
    }

    for (uint32_t i = 0; i < module->segment_count; ++i) {
        const struct minemu_module_segment *segment = &segments[i];
        if (segment->memory_size == 0 && segment->file_size == 0) {
            continue; /* e.g. a program with no data or BSS */
        }
        if (!segment_valid(segment, boot_info->image_size)) {
            return 0;
        }
        /* Pages arrive zeroed, so the BSS tail needs no extra clearing. */
        if (!allocate_range(segment->virtual_address, segment->memory_size,
                            pte_flags_from_segment(segment->flags))) {
            return 0;
        }
        fill_range(segment->virtual_address, image + segment->data_offset, segment->file_size);
        if ((segment->flags & MINEMU_SEGMENT_EXECUTABLE) != 0 && segment->file_size >= 4U &&
            module->entry_vaddr >= segment->virtual_address &&
            module->entry_vaddr - segment->virtual_address <= segment->file_size - 4U) {
            entry_is_executable = true;
        }
    }
    if (!entry_is_executable) {
        return 0;
    }

    if (!allocate_range(USPACE_STACK_BOTTOM, USPACE_STACK_TOP - USPACE_STACK_BOTTOM,
                        MINEMU_PTE_READABLE | MINEMU_PTE_WRITABLE)) {
        return 0;
    }

    page_directory()[USPACE_IMAGE_BASE >> 22] =
        kernel_to_paddr(user_page_table) | MINEMU_PDE_VALID;
    minemu_mmu_tlbiall();
    return module->entry_vaddr;
}

/*
 * Look up the PTE for one user page, or 0 if it is outside the user window.
 * Only the table this file built is consulted, so a pointer into kernel
 * memory, MMIO or the system ROM never translates.
 */
static uint32_t user_pte(uint32_t vaddr) {
    if (user_page_table == NULL || (vaddr >> 22) != (USPACE_IMAGE_BASE >> 22)) {
        return 0;
    }
    return user_page_table[(vaddr >> 12) & 1023U];
}

static bool range_has(uint32_t vaddr, uint32_t length, uint32_t required) {
    if (length == 0) {
        return true;
    }
    if (vaddr + length < vaddr) {
        return false; /* wraps around the address space */
    }
    uint32_t last = (vaddr + length - 1U) & PAGE_MASK;
    for (uint32_t page = vaddr & PAGE_MASK;; page += PAGE_SIZE) {
        if ((user_pte(page) & required) != required) {
            return false;
        }
        if (page == last) {
            return true;
        }
    }
}

bool uspace_readable(uint32_t vaddr, uint32_t length) {
    return range_has(vaddr, length, MINEMU_PTE_VALID | MINEMU_PTE_USER | MINEMU_PTE_READABLE);
}

bool uspace_writable(uint32_t vaddr, uint32_t length) {
    return range_has(vaddr, length, MINEMU_PTE_VALID | MINEMU_PTE_USER | MINEMU_PTE_WRITABLE);
}

/*
 * Kernel alias of a user byte: translate through the user page table and
 * reach the physical page through the kernel's direct map. Going through
 * the page table (instead of dereferencing the user VA) means the kernel
 * touches exactly the page the check approved.
 */
static uint8_t *user_alias(uint32_t vaddr) {
    uint32_t paddr = (user_pte(vaddr) & PAGE_MASK) | (vaddr & ~PAGE_MASK);
    return (uint8_t *)paddr_to_kernel(paddr);
}

bool uspace_copy_from(void *dst, uint32_t user_src, uint32_t length) {
    if (!uspace_readable(user_src, length)) {
        return false;
    }
    uint8_t *out = dst;
    for (uint32_t i = 0; i < length; ++i) {
        out[i] = *user_alias(user_src + i);
    }
    return true;
}

bool uspace_copy_to(uint32_t user_dst, const void *src, uint32_t length) {
    if (!uspace_writable(user_dst, length)) {
        return false;
    }
    const uint8_t *in = src;
    for (uint32_t i = 0; i < length; ++i) {
        *user_alias(user_dst + i) = in[i];
    }
    return true;
}
