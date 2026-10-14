.syntax unified
.cpu cortex-a9
.arm

/*
 * SVC vector entry. The CPU arrives here in SVC mode after "svc #0" with:
 *   lr_svc   = address of the instruction after the svc
 *   spsr_svc = the caller's CPSR (USR mode for a real syscall)
 *   CPSR.I   = 1, so IRQs stay masked for the whole syscall
 *
 * Builds a struct minemu_trap_frame (88 bytes, see trap.h) on the SVC stack,
 * calls minemu_svc_dispatch(frame), and resumes the frame it returns. The
 * dispatcher puts the syscall result in frame->r[0], which is restored into
 * r0 below. Same frame layout as the IRQ trampoline (src/irq/trampoline.S).
 */

.global minemu_svc_trampoline
.type minemu_svc_trampoline, %function
.extern minemu_svc_dispatch

minemu_svc_trampoline:
    sub sp, sp, #88
    stmia sp, {r0-r12}          @ r[0..12]: syscall args in r0-r6, number in r7
    str lr, [sp, #52]           @ return_lr = instruction after svc
    mrs r1, spsr
    str r1, [sp, #56]           @ spsr = caller CPSR
    ldr r1, =-3
    str r1, [sp, #60]           @ exception_id = MINEMU_EXCEPTION_SVC
    sub r1, lr, #4
    str r1, [sp, #64]           @ fault_pc = address of the svc itself
    mov r1, #0
    str r1, [sp, #68]           @ dfsr (unused for SVC)
    str r1, [sp, #72]           @ dfar (unused for SVC)
    add r1, sp, #76
    stmia r1, {sp, lr}^         @ user_sp, user_lr (banked USR registers)
    mov r1, #0
    str r1, [sp, #84]           @ reserved

    mov r0, sp
    bl minemu_svc_dispatch
    mov sp, r0                  @ resume whichever frame the dispatcher chose

    ldr lr, [sp, #52]
    ldr r1, [sp, #56]
    msr spsr_cxsf, r1
    add r1, sp, #76
    ldmia r1, {sp, lr}^
    ldmia sp, {r0-r12}          @ r0 now holds the syscall result
    add sp, sp, #88
    movs pc, lr                 @ return after the svc, restoring CPSR
