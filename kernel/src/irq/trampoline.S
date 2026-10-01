.syntax unified
.cpu cortex-a9
.arm

/*
 * IRQ vector entry. The vector table branches here in IRQ mode with
 * lr_irq = interrupted PC + 4 and spsr_irq = interrupted CPSR.
 *
 * Builds a struct minemu_trap_frame (88 bytes, see trap.h) on the IRQ stack,
 * claims the interrupt source into frame->exception_id, and calls
 * minemu_irq_dispatch(frame). The dispatcher returns the frame to resume,
 * which is the same frame until the kernel starts switching tasks.
 */

.global minemu_irq_trampoline
.type minemu_irq_trampoline, %function
.extern minemu_irq_dispatch

minemu_irq_trampoline:
    sub sp, sp, #88
    stmia sp, {r0-r12}          @ r[0..12]
    str lr, [sp, #52]           @ return_lr
    mrs r1, spsr
    str r1, [sp, #56]           @ spsr
    ldr r1, =0x10000000         @ MINEMU_INTERRUPT_BASE
    ldr r1, [r1, #8]            @ read CLAIM: highest-priority pending source
    str r1, [sp, #60]           @ exception_id = claimed source
    sub r1, lr, #4
    str r1, [sp, #64]           @ fault_pc = interrupted instruction
    mov r1, #0
    str r1, [sp, #68]           @ dfsr (unused for IRQs)
    str r1, [sp, #72]           @ dfar (unused for IRQs)
    add r1, sp, #76
    stmia r1, {sp, lr}^         @ user_sp, user_lr (banked USR/SYS registers)
    mov r1, #0
    str r1, [sp, #84]           @ reserved

    mov r0, sp
    bl minemu_irq_dispatch
    mov sp, r0                  @ resume whichever frame the dispatcher chose

    ldr lr, [sp, #52]
    ldr r1, [sp, #56]
    msr spsr_cxsf, r1
    add r1, sp, #76
    ldmia r1, {sp, lr}^
    ldmia sp, {r0-r12}
    add sp, sp, #88
    subs pc, lr, #4             @ return to interrupted PC, restoring CPSR
