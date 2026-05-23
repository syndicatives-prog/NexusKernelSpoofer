.code
extern VmexitHandler : proc
PUBLIC VmxExitEntry
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
    .endprolog
    vmlaunch
    mov rax, 0C0000001h
    jmp done
VmxExitEntry PROC
    sub rsp, 28h
    mov rcx, 4402h
    vmread rax, rcx
    mov rcx, rax
    mov rdx, 681Eh
    vmread rdx, rdx
    call VmexitHandler
    add rsp, 28h
    vmresume
    int 3
VmxExitEntry ENDP
done:
    pop r15
    pop r14
    pop r13
    pop r12
    pop rsi
    pop rdi
    pop rbx
    ret
VmxLaunch ENDP
END
