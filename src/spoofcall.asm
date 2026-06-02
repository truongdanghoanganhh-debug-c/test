; spoofcall.asm — Return Address Spoof Stub (MASM x64)
;
; This stub replaces the real return address with a gadget
; found inside the game module, making the call stack appear
; as if only game code is calling engine functions.
;
; Magic sentinel: 0x52A3450
; Exports: spoofcall_stub, proxy_call_returns, proxy_call_fakestack, proxy_call_fakestack_size

.data

ALIGN 8
PUBLIC proxy_call_returns
proxy_call_returns  QWORD 32 DUP(0)    ; Pre-populated with gadget addresses

PUBLIC proxy_call_fakestack
proxy_call_fakestack QWORD 0            ; Pointer to fake callstack array

PUBLIC proxy_call_fakestack_size
proxy_call_fakestack_size QWORD 0       ; Number of entries in fake callstack

MAGIC_SENTINEL EQU 052A3450h

.code

; ─── spoofcall_stub ───────────────────────────────────────────────────
; Called as: stub(arg0, arg1, arg2, arg3, ..., MAGIC_SENTINEL, funcPtr)
;
; The function pointer is the LAST argument on the stack.
; We find it by scanning stack for MAGIC_SENTINEL, take the next QWORD
; as the real function pointer.
;
; Registers on entry (x64 __fastcall):
;   RCX = arg0, RDX = arg1, R8 = arg2, R9 = arg3
;   Stack: [ret addr] [shadow0-3] [arg4] [arg5] ... [MAGIC] [funcPtr]
;
PUBLIC spoofcall_stub
spoofcall_stub PROC

    ; Save non-volatile registers
    push    rbx
    push    rsi
    push    rdi
    push    rbp
    push    r12
    push    r13
    push    r14
    push    r15
    sub     rsp, 28h                ; shadow space + alignment

    ; Save original arguments
    mov     r12, rcx                ; arg0
    mov     r13, rdx                ; arg1
    mov     r14, r8                 ; arg2
    mov     r15, r9                 ; arg3

    ; Find the magic sentinel on the stack
    ; Start scanning from RSP + saved regs + shadow = reasonable offset
    lea     rsi, [rsp + 28h + 40h]  ; past our saves + shadow
    xor     rbx, rbx                ; counter

scan_loop:
    mov     rax, [rsi + rbx * 8]
    cmp     eax, MAGIC_SENTINEL     ; compare low 32 bits
    je      found_magic
    inc     rbx
    cmp     rbx, 30                 ; scan up to 30 QWORDS
    jb      scan_loop

    ; Magic not found — fall through to direct call with r9 as funcPtr
    ; (This happens when <= 4 args: magic is in register territory)
    ; Check R9 for magic
    cmp     r9d, MAGIC_SENTINEL
    je      magic_in_r9

    ; Last resort: just call with whatever is in the last register position
    ; This shouldn't happen in practice
    jmp     cleanup_and_ret

magic_in_r9:
    ; funcPtr was pushed as [rsp + stack arg area]
    ; With 6 args in fastcall: RCX=0, RDX=1, R8=2, R9=MAGIC, stack[0]=funcPtr
    mov     rax, [rsp + 28h + 40h + 20h]   ; first stack arg after shadow
    jmp     do_call

found_magic:
    ; funcPtr is the next QWORD after magic
    mov     rax, [rsi + rbx * 8 + 8]

do_call:
    ; RAX = real function pointer
    mov     rbp, rax                ; save function pointer

    ; Overwrite our return address with a gadget from proxy_call_returns
    ; Use a simple rotating index based on low bits of function pointer
    mov     rax, rbp
    and     eax, 1Fh                ; index = funcPtr & 31
    lea     rdi, proxy_call_returns
    mov     rax, [rdi + rax * 8]
    test    rax, rax
    jz      direct_call             ; no gadget available

    ; Store the real return address so we can restore after
    mov     r10, [rsp + 28h + 38h]  ; original return address (after our pushes)

    ; Restore original arguments
    mov     rcx, r12
    mov     rdx, r13
    mov     r8, r14
    mov     r9, r15

    ; Clean up our frame
    add     rsp, 28h
    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     rbp
    pop     rdi
    pop     rsi
    pop     rbx

    ; Jump to the actual function (tail call)
    jmp     qword ptr [rsp - 48h]    ; jump to rbp (saved function ptr)

direct_call:
    ; No gadget — direct call
    mov     rcx, r12
    mov     rdx, r13
    mov     r8, r14
    mov     r9, r15

    add     rsp, 28h
    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     rbp
    pop     rdi
    pop     rsi
    pop     rbx

    jmp     rbp                     ; tail call to function

cleanup_and_ret:
    xor     eax, eax
    add     rsp, 28h
    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     rbp
    pop     rdi
    pop     rsi
    pop     rbx
    ret

spoofcall_stub ENDP

END
