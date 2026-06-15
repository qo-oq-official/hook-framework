;; thunk_x64.asm - x64 hook trampoline
;; Entry: RAX=ThunkData*, RCX/RDX/R8/R9=args, [RSP]=retaddr
;; RSP at entry is 8-mod-16 (standard after CALL)

.code

EXTERN hookfw_dispatch:PROC

hookfw_thunk_entry PROC
    ;; --- Frame setup ---
    push rbp                    ; RSP: 8->0 mod16
    mov rbp, rsp
    push rbx                   ; RSP: 0->8
    push rsi                   ; RSP: 8->0
    push rdi                   ; RSP: 0->8
    push r12                   ; RSP: 8->0
    push r13                   ; RSP: 0->8
    push r14                   ; RSP: 8->0
    push r15                   ; RSP: 0->8
    sub rsp, 8                 ; RSP: 8->0 mod16 (alignment pad)

    ;; Now RSP is 0-mod-16

    ;; Save ThunkData* and original args in callee-saved regs
    mov r12, rax               ; ThunkData*
    mov r13, rcx               ; arg0
    mov r14, rdx               ; arg1
    mov r15, r8                ; arg2
    mov rbx, r9                ; arg3
    lea rsi, [rbp + 16]        ; original RSP (ret addr location)

    ;; Save XMM args (RSP is 0-mod-16, movaps ok)
    sub rsp, 64                ; RSP: 0->0 mod16 (64 is multiple of 16)
    movaps [rsp],      xmm0
    movaps [rsp + 16], xmm1
    movaps [rsp + 32], xmm2
    movaps [rsp + 48], xmm3

    ;; RSP is still 0-mod-16

    ;; --- Pre-hook dispatch ---
    ;; hookfw_dispatch(ThunkData*, arg0, arg1, arg2, arg3, origRSP, phase, xmmPtr)
    ;; Need 32 shadow + 5*8 stack args = 72 bytes. Use 80 for 16-alignment.
    sub rsp, 80                ; RSP: 0->0 mod16
    mov rcx, r12               ; ThunkData*
    mov rdx, r13               ; arg0
    mov r8, r14                ; arg1
    mov r9, r15                ; arg2
    mov qword ptr [rsp + 32], rbx   ; arg3
    mov qword ptr [rsp + 40], rsi   ; origRSP
    mov qword ptr [rsp + 48], 0     ; phase=0 (entry)
    lea rax, [rsp + 80]            ; xmmPtr -> XMM save area
    mov qword ptr [rsp + 56], rax   ; arg8 = xmmPtr
    call hookfw_dispatch
    add rsp, 80

    ;; Restore XMM args
    movaps xmm0, [rsp]
    movaps xmm1, [rsp + 16]
    movaps xmm2, [rsp + 32]
    movaps xmm3, [rsp + 48]
    add rsp, 64

    ;; RSP is 0-mod-16 again

    ;; Restore integer args
    mov rcx, r13
    mov rdx, r14
    mov r8, r15
    mov r9, rbx

    ;; --- Call target function ---
    ;; Check for replacement
    mov rax, [r12 + 28h]       ; replacementFunction
    test rax, rax
    jz do_call_original

    ;; Has replacement
    movzx edi, byte ptr [r12 + 30h]  ; callOriginal
    test edi, edi
    jnz do_call_both

    ;; Only replacement (RSP is 0-mod-16, need 32 shadow, use 48 for alignment)
    sub rsp, 48                ; RSP: 0->0 mod16
    call rax
    add rsp, 48
    jmp after_call

do_call_both:
    ;; Call original first
    sub rsp, 48
    mov rax, [r12 + 08h]      ; trampolineFunction
    call rax
    add rsp, 48
    mov rsi, rax               ; save return value
    ;; Restore args for replacement
    mov rcx, r13
    mov rdx, r14
    mov r8, r15
    mov r9, rbx
    sub rsp, 48
    mov rax, [r12 + 28h]
    call rax
    add rsp, 48
    jmp after_call

do_call_original:
    sub rsp, 48                ; RSP: 0->0 mod16
    mov rax, [r12 + 08h]      ; trampolineFunction
    call rax
    add rsp, 48

after_call:
    ;; RAX = int return, XMM0 = float return
    mov rsi, rax

    ;; Save float return on stack (XMM4 is volatile, can't survive call)
    sub rsp, 16
    movaps [rsp], xmm0

    ;; --- Post-hook dispatch ---
    sub rsp, 80                ; RSP: 0->0 mod16
    mov rcx, r12               ; ThunkData*
    mov rdx, rsi               ; return value (in arg slot)
    xor r8, r8
    xor r9, r9
    mov qword ptr [rsp + 32], 0
    mov qword ptr [rsp + 40], 0
    mov qword ptr [rsp + 48], 1     ; phase=1 (exit)
    lea rax, [rsp + 80]            ; xmmPtr -> saved XMM0 return
    mov qword ptr [rsp + 56], rax   ; arg8 = xmmPtr
    call hookfw_dispatch
    add rsp, 80

    ;; Restore return values
    movaps xmm0, [rsp]
    add rsp, 16
    mov rax, rsi

    ;; --- Epilogue ---
    add rsp, 8                 ; remove alignment pad
    pop r15
    pop r14
    pop r13
    pop r12
    pop rdi
    pop rsi
    pop rbx
    pop rbp
    ret

hookfw_thunk_entry ENDP

END
