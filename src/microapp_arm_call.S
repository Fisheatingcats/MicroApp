/** @file microapp_arm_call.S
 * @brief Thumb AAPCS bridge; public parameter contract is in microapp_native.h.
 * @details Six host arguments arrive as r0-r3 plus two stack words. Saving r9/lr
 * consumes eight bytes, preserving caller-provided 8-byte stack alignment. The
 * App receives its four arguments in r0-r3 and returns through r0; r9 is restored.
 */
.syntax unified
.cpu cortex-m4
.thumb
.text
.align 2
.global microapp_arm_call
.type microapp_arm_call, %function
.thumb_func
microapp_arm_call:
    push {r9, lr} /* Preserve caller PIC base and return address, keeping alignment. */
    mov r9, r0
    mov r12, r1
    mov r0, r2
    mov r1, r3
    ldr r2, [sp, #8] /* Skip saved r9/lr to reach the host's fifth argument. */
    ldr r3, [sp, #12]
    blx r12
    pop {r9, pc}
.size microapp_arm_call, .-microapp_arm_call
.section .note.GNU-stack,"",%progbits
