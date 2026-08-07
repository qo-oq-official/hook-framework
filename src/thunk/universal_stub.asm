;; universal_stub.asm - minimal assembly stub that saves registers
;; and calls UniversalCxxBridge(ThunkData*, RegisterContext*)
;;
;; RegisterContext layout (80 bytes, packed):
;;   +0x00: rax
;;   +0x08: rcx, rdx, r8, r9
;;   +0x28: rsp (caller RSP)
;;   +0x30: xmm0-3 (4×8 = 32 bytes)
;;   = 0x50 total
;;
;; Entry: RAX = ThunkData* (loaded by generateStub trampoline)
;;        RCX,RDX,R8,R9 = original function arguments
;;
;; Exit:  RAX = int return value (from ctx.rax)
;;        XMM0 = float return value (from ctx.xmm0)

.code

EXTERN UniversalCxxBridge:PROC

hookfw_universal_entry PROC FRAME
    ;; Prolog: 0x70 bytes for RegisterContext + alignment + shadow space
    push rbp
    .pushreg rbp
    mov rbp, rsp
    .setframe rbp, 0
    sub rsp, 0x70
    .allocstack 0x70
    .endprolog

    ;; Save registers to RegisterContext at [rsp + 0x20] (past shadow space)
    mov [rsp+20h+00h], rax          ; ctx.rax
    mov [rsp+20h+08h], rcx          ; ctx.rcx
    mov [rsp+20h+10h], rdx          ; ctx.rdx
    mov [rsp+20h+18h], r8           ; ctx.r8
    mov [rsp+20h+20h], r9           ; ctx.r9
    movq [rsp+20h+30h], xmm0        ; ctx.xmm0
    movq [rsp+20h+38h], xmm1        ; ctx.xmm1
    movq [rsp+20h+40h], xmm2        ; ctx.xmm2
    movq [rsp+20h+48h], xmm3        ; ctx.xmm3

    ;; Caller RSP = our RBP + 16 (8 for pushed rbp + 8 for return address)
    lea rcx, [rbp + 16]
    mov [rsp+20h+28h], rcx          ; ctx.rsp

    ;; Prepare args for UniversalCxxBridge
    ;; rcx = ThunkData* (from saved rax = original entry rax)
    mov rcx, rax
    ;; rdx = &RegisterContext
    lea rdx, [rsp + 20h]
    sub rsp, 32                     ; shadow space
    call UniversalCxxBridge
    add rsp, 32

    ;; Restore return values from RegisterContext
    movq xmm0, [rsp+20h+30h]
    mov rax, [rsp+20h+00h]

    ;; Epilog
    add rsp, 0x70
    pop rbp
    ret

hookfw_universal_entry ENDP

END
