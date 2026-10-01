#include "minemu/boot.h"
#include "minemu/irq.h"
#include "minemu/kirq.h"
#include "minemu/kprintf.h"
#include "minemu/msh.h"
#include "minemu/platform.h"
#include "minemu/trap.h"
#include "minemu/trace.h"
#include "minemu/uart.h"

void minemu_kernel_main(const struct minemu_boot_info *boot_info) {
    if ((uintptr_t)boot_info != MINEMU_BOOT_INFO_VADDR ||
        boot_info->magic != MINEMU_BOOT_INFO_MAGIC ||
        boot_info->version != MINEMU_ABI_VERSION ||
        boot_info->size != sizeof(*boot_info) ||
        boot_info->system_rom_base != UINT32_C(0x08000000) ||
        boot_info->direct_map_vaddr != UINT32_C(0xc0000000) ||
        boot_info->direct_map_paddr != UINT32_C(0x40000000) ||
        boot_info->direct_map_size != UINT32_C(0x04000000)) {
        minemu_trace_event(UINT32_C(0xb007bad0));
        minemu_fail_stop();
    }

    /*
     * Interrupt bring-up. CPU IRQs are still masked from boot, so the order of
     * the first five steps is free; they only become live at the last step.
     */
    irq_init();                                         /* all sources masked, no handlers */
    uart_init();                                        /* empty RX buffer, RX IRQ off */
    irq_register_handler(MINEMU_IRQ_UART0, uart_rx_isr);
    uart_enable_rx_irq();                               /* UART: CONTROL.RX_IRQ_ENABLE = 1 */
    irq_enable_source(MINEMU_IRQ_UART0);                /* controller: ENABLE = 1 << UART0 */
    minemu_irq_enable();                                /* CPU: clear CPSR.I */

    kprintf("hello world\n");
    minemu_trace_event(1);

    msh_run();
}
