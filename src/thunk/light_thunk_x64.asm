;; light_thunk_x64.asm - Lightweight hook trampoline (trace mode)
;; Zero persistent stack: saves args, calls quickTrace, restores, JMPs to original
;;
;; Entry: RAX = ThunkData*, RCX/RDX/R8/R9 = original args, [RSP] = return addr
;; RSP at entry is 8-mod-16

.code

EXTERN hookfw_quick_trace:PROC

hookfw_light_entry PROC
    ;; Entry RSP = 8-mod-16
    ;; Need: 32 shadow + 40 saved (ThunkData* + 4 args) = 72
    ;; After sub 72: RSP = 8-72 = -64 ≡ 0 mod 16. Good for CALL.
    sub rsp, 72

    mov [rsp+64], rax         ; save ThunkData*
    mov [rsp+56], rcx         ; save arg0
    mov [rsp+48], rdx         ; save arg1
    mov [rsp+40], r8          ; save arg2
    mov [rsp+32], r9          ; save arg3 (doubles as 5th call param)

    ;; hookfw_quick_trace(ThunkData*, arg0, arg1, arg2, arg3)
    mov rcx, rax              ; ThunkData*
    mov rdx, [rsp+56]         ; arg0
    mov r8, [rsp+48]          ; arg1
    mov r9, [rsp+40]          ; arg2
    ;; [rsp+32] = arg3 already set
    call hookfw_quick_trace

    ;; Restore original args
    mov rcx, [rsp+56]
    mov rdx, [rsp+48]
    mov r8, [rsp+40]
    mov r9, [rsp+32]
    mov rax, [rsp+64]         ; ThunkData*

    ;; Get trampoline address
    mov rax, [rax+08h]        ; ThunkData::trampolineFunction

    ;; Cleanup and tail-jump to original function
    ;; Return addr still on stack at [rsp+72] - trampoline returns to original caller
    add rsp, 72
    jmp rax

hookfw_light_entry ENDP

END
