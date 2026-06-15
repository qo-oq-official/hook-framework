;; light_thunk_x64.asm - Lightweight hook trampoline with B/E capture
;; Records entry (B) and exit (E) events for Perfetto call-stack view
;;
;; Entry: RAX = ThunkData*, RCX/RDX/R8/R9 = original args, [RSP] = return addr
;; RSP at entry is 8-mod-16
;; Stack per nesting level: ~112 bytes

.code

EXTERN hookfw_quick_trace:PROC
EXTERN hookfw_quick_trace_exit:PROC

hookfw_light_entry PROC
    ;; Entry RSP = 8-mod-16
    ;; sub 88: RSP = 8-88 = -80 ≡ 0 mod16
    sub rsp, 88

    mov [rsp+80], rax         ; save ThunkData*
    mov [rsp+72], rcx         ; save arg0
    mov [rsp+64], rdx         ; save arg1
    mov [rsp+56], r8          ; save arg2
    mov [rsp+48], r9          ; save arg3

    ;; ---- Entry trace (produces "B" event) ----
    mov rcx, rax              ; ThunkData*
    mov rdx, [rsp+72]         ; arg0
    mov r8, [rsp+64]          ; arg1
    mov r9, [rsp+56]          ; arg2
    mov rax, [rsp+48]
    mov [rsp+32], rax         ; arg3 (5th param)
    call hookfw_quick_trace

    ;; Restore original args
    mov rcx, [rsp+72]
    mov rdx, [rsp+64]
    mov r8, [rsp+56]
    mov r9, [rsp+48]

    ;; Forward caller's stack args using r10 (volatile, no save needed)
    ;; Our frame is 88 bytes. Return addr at [rsp+88].
    ;; Original shadow at [rsp+96..127]. 5th param at [rsp+128].
    lea r10, [rsp+128]

    sub rsp, 96               ; 32 shadow + 64 forwarded args. RSP: 0-96=0 mod16
    mov rax, [r10]
    mov [rsp+32], rax
    mov rax, [r10+8]
    mov [rsp+40], rax
    mov rax, [r10+16]
    mov [rsp+48], rax
    mov rax, [r10+24]
    mov [rsp+56], rax
    mov rax, [r10+32]
    mov [rsp+64], rax
    mov rax, [r10+40]
    mov [rsp+72], rax
    mov rax, [r10+48]
    mov [rsp+80], rax
    mov rax, [r10+56]
    mov [rsp+88], rax

    ;; CALL trampoline
    mov rax, [rsp+96+80]      ; ThunkData* (at offset 96+80=176 from current rsp)
    mov rax, [rax+08h]        ; trampolineFunction
    call rax
    add rsp, 96               ; RSP back to 0-mod-16

    ;; Save return value
    mov [rsp+40], rax         ; use free slot for retval

    ;; ---- Exit trace (produces "E" event) ----
    mov rcx, [rsp+80]        ; ThunkData*
    mov rdx, rax             ; return value
    xor r8, r8
    xor r9, r9
    mov qword ptr [rsp+32], 0
    call hookfw_quick_trace_exit

    ;; Restore return value
    mov rax, [rsp+40]

    add rsp, 88
    ret

hookfw_light_entry ENDP

END
