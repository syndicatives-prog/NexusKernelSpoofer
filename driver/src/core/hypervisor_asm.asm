.code

extern VmexitHandler : proc
extern InitVmcsGuestState : proc

PUBLIC VmxExitEntry
PUBLIC VmxLaunch

VmxLaunch PROC FRAME
    push rbx
    .pushreg rbx
    push rdi
    .pushreg rdi
    push rsi
    .pushreg rsi
    push r12
    .pushreg r12
    push r13
    .pushreg r13
    push r14
    .pushreg r14
    push r15
    .pushreg r15
    sub  rsp, 20h
    .allocstack 20h
    .endprolog

    ; Escribir GUEST_RSP
    mov  rax, rsp
    mov  rcx, 681Ch
    vmwrite rcx, rax

    ; Escribir GUEST_RIP
    lea  rax, guest_return_point
    mov  rcx, 681Eh
    vmwrite rcx, rax

    ; Escribir GUEST_RFLAGS
    pushfq
    pop  rax
    or   rax, 200h
    and  rax, 0FFFFFFFFFFFFFFFEh
    mov  rcx, 6820h
    vmwrite rcx, rax

    vmlaunch

    ; VMLAUNCH fall? ? leer error code
    mov  rcx, 4400h
    vmread rcx, rcx
    add  rsp, 20h
    pop  r15
    pop  r14
    pop  r13
    pop  r12
    pop  rsi
    pop  rdi
    pop  rbx
    mov  rax, 0C0000001h
    ret

guest_return_point:
    add  rsp, 20h
    pop  r15
    pop  r14
    pop  r13
    pop  r12
    pop  rsi
    pop  rdi
    pop  rbx
    xor  eax, eax
    ret

VmxLaunch ENDP


VmxExitEntry PROC
    push rax
    push rbx
    push rcx
    push rdx
    push r8
    push r9
    push r10
    push r11
    push rdi
    push rsi
    push rbp
    push r12
    push r13
    push r14
    push r15

    sub  rsp, 20h

    mov  rax, 4402h
    vmread rcx, rax

    mov  rax, 681Eh
    vmread rdx, rax

    lea  r8, [rsp + 20h]

    call VmexitHandler

    add  rsp, 20h

    pop  r15
    pop  r14
    pop  r13
    pop  r12
    pop  rbp
    pop  rsi
    pop  rdi
    pop  r11
    pop  r10
    pop  r9
    pop  r8
    pop  rdx
    pop  rcx
    pop  rbx
    pop  rax

    vmresume
    int  3

VmxExitEntry ENDP

END
