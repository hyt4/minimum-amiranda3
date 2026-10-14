#include "minemu/boot.h"
#include "minemu/irq.h"
#include "minemu/kirq.h"
#include "minemu/platform.h"
#include "minemu/trap.h"
#include "minemu/trace.h"
#include "minemu/uart.h"
#include "minemu/uspace.h"

/* The image module started at boot. */
#define INIT_MODULE "minimum-user"

#define TRACE_BAD_BOOT_INFO UINT32_C(0xb007bad0)
#define TRACE_BAD_MODULE UINT32_C(0xb007bad1)

void minemu_kernel_main(const struct minemu_boot_info *boot_info) {
    if ((uintptr_t)boot_info != MINEMU_BOOT_INFO_VADDR ||
        boot_info->magic != MINEMU_BOOT_INFO_MAGIC ||
        boot_info->version != MINEMU_ABI_VERSION ||
        boot_info->size != sizeof(*boot_info) ||
        boot_info->system_rom_base != UINT32_C(0x08000000) ||
        boot_info->direct_map_vaddr != UINT32_C(0xc0000000) ||
        boot_info->direct_map_paddr != UINT32_C(0x40000000) ||
        boot_info->direct_map_size != UINT32_C(0x04000000) ||
        !uspace_map_image(boot_info)) {
        minemu_trace_event(TRACE_BAD_BOOT_INFO);
        minemu_fail_stop();
    }

    /*
     * Interrupt bring-up. CPU IRQs stay masked in the kernel; they are first
     * unmasked by the switch to USR mode, whose CPSR has I = 0.
     */
    irq_init();                                         /* all sources masked, no handlers */
    uart_init();                                        /* empty RX buffer, RX IRQ off */
    irq_register_handler(MINEMU_IRQ_UART0, uart_rx_isr);
    uart_enable_rx_irq();                               /* UART: CONTROL.RX_IRQ_ENABLE = 1 */
    irq_enable_source(MINEMU_IRQ_UART0);                /* controller: ENABLE = 1 << UART0 */

    uint32_t entry = uspace_load(boot_info, INIT_MODULE);
    if (entry == 0) {
        minemu_trace_event(TRACE_BAD_MODULE);
        minemu_fail_stop();
    }
    uspace_enter(entry, USPACE_STACK_TOP);
}
