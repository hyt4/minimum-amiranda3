.syntax unified
.cpu cortex-a9
.arm

/*
 * void uspace_enter(uint32_t entry, uint32_t stack_top)
 *
 * Drop from SVC to USR mode at `entry` with SP_usr = `stack_top`.
 * Adapted from examples/user-mode-hello/kernel/user.S.
 *
 * SP and LR are banked, so the user copies are set with the ^ form of LDM,
 * which loads the USR-mode registers from privileged code. The final
 * "movs pc, lr" copies SPSR into CPSR: mode = USR, A32, IRQs enabled.
 * The SVC stack is left empty, so the next exception starts from its top.
 */

.global uspace_enter
.type uspace_enter, %function
.extern __minemu_svc_stack_top

uspace_enter:
    cpsid if
    mov r2, r0

    sub sp, sp, #8
    str r1, [sp]                @ SP_usr = stack_top
    mov r3, #0
    str r3, [sp, #4]            @ LR_usr = 0
    mov r3, sp
    ldmia r3, {sp, lr}^
    ldr sp, =__minemu_svc_stack_top

    mov r3, #0x10               @ USR mode, ARM state, I = F = 0
    msr spsr_cxsf, r3
    mov lr, r2
    mov r0, #0
    mov r1, #0
    mov r2, #0
    mov r3, #0
    mov r12, #0
    movs pc, lr
