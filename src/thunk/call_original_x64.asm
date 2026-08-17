;; call_original_x64.asm - forwards the full argument list to the original
;; function and captures its return value back into the RegisterContext.
;;
;; C signature:
;;   extern "C" uint64_t hookfw_call_original(ThunkData* data, RegisterContext* ctx);
;;
;; Entry: RCX = ThunkData*, RDX = RegisterContext*
;;
;; What it does:
;;   * Restores integer args RCX/RDX/R8/R9 and float args XMM0..XMM3 from
;;     RegisterContext before the call.
;;   * Copies the caller's stack args (5th and beyond) from the original
;;     caller's RSP (RegisterContext.rsp) onto the outgoing call frame.
;;   * Calls data->trampolineFunction (offset 0x08).
;;   * Writes the return value back: RAX -> RegisterContext.rax (offset 0x00)
;;     and XMM0 -> RegisterContext.xmm0 (offset 0x30).
;;
;; Full unwind metadata is emitted (.pdata/.xdata) so a C++ exception thrown by
;; the original function unwinds through this frame into the caller's catch.
;;
;; RegisterContext layout (see universal_bridge.h, packed):
;;   +0x00 rax
;;   +0x08 rcx, +0x10 rdx, +0x18 r8, +0x20 r9
;;   +0x28 rsp  (original caller RSP)
;;   +0x30 xmm0, +0x38 xmm1, +0x40 xmm2, +0x48 xmm3

.code

hookfw_call_original PROC FRAME
    push rbp
    .pushreg rbp
    mov rbp, rsp
    .setframe rbp, 0
    sub rsp, 80h
    .allocstack 80h
    .endprolog

    ;; Save incoming ThunkData* (rcx) and RegisterContext* (rdx) into locals.
    mov [rbp - 20h], rcx
    mov [rbp - 18h], rdx

    ;; Forward stack args (5th..12th). RegisterContext.rsp is the original
    ;; caller's RSP; the 5th arg lives at [callerRSP + 32] = [r10 + 20h].
    mov r10, [rdx + 28h]
    mov rax, [r10 + 20h]
    mov [rbp - 60h], rax
    mov rax, [r10 + 28h]
    mov [rbp - 58h], rax
    mov rax, [r10 + 30h]
    mov [rbp - 50h], rax
    mov rax, [r10 + 38h]
    mov [rbp - 48h], rax
    mov rax, [r10 + 40h]
    mov [rbp - 40h], rax
    mov rax, [r10 + 48h]
    mov [rbp - 38h], rax
    mov rax, [r10 + 50h]
    mov [rbp - 30h], rax
    mov rax, [r10 + 58h]
    mov [rbp - 28h], rax

    ;; Reload RegisterContext* into r11 (rcx/rdx are repurposed below).
    mov r11, [rbp - 18h]

    ;; Restore integer args (arg 0..3).
    mov rcx, [r11 + 08h]
    mov rdx, [r11 + 10h]
    mov r8,  [r11 + 18h]
    mov r9,  [r11 + 20h]

    ;; Restore float args (arg 0..3 in XMM).
    movq xmm0, qword ptr [r11 + 30h]
    movq xmm1, qword ptr [r11 + 38h]
    movq xmm2, qword ptr [r11 + 40h]
    movq xmm3, qword ptr [r11 + 48h]

    ;; Call the Detours trampoline (ThunkData.trampolineFunction).
    mov rax, [rbp - 20h]
    mov rax, [rax + 08h]
    call rax

    ;; Save return values back into RegisterContext.
    mov r11, [rbp - 18h]
    mov [r11 + 00h], rax
    movq qword ptr [r11 + 30h], xmm0

    add rsp, 80h
    pop rbp
    ret

hookfw_call_original ENDP

END
