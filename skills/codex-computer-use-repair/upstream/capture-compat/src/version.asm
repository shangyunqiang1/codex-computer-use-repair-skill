option casemap:none
extern ResolveVersionExport:proc
.code

; Preserve integer/XMM argument registers and leave all stack arguments in place.
; A tail jump also forwards the undocumented ByHandle export without inventing
; its prototype. FRAME metadata makes unwinding through the resolver valid.
ForwardVersion proc frame
    sub rsp, 0A8h
    .allocstack 0A8h
    .endprolog
    mov [rsp+20h], rcx
    mov [rsp+28h], rdx
    mov [rsp+30h], r8
    mov [rsp+38h], r9
    movdqu [rsp+40h], xmm0
    movdqu [rsp+50h], xmm1
    movdqu [rsp+60h], xmm2
    movdqu [rsp+70h], xmm3
    mov ecx, eax
    call ResolveVersionExport
    mov r11, rax
    mov rcx, [rsp+20h]
    mov rdx, [rsp+28h]
    mov r8, [rsp+30h]
    mov r9, [rsp+38h]
    movdqu xmm0, [rsp+40h]
    movdqu xmm1, [rsp+50h]
    movdqu xmm2, [rsp+60h]
    movdqu xmm3, [rsp+70h]
    add rsp, 0A8h
    jmp r11
ForwardVersion endp

FORWARD macro n
Proxy&n proc
    mov eax, n
    jmp ForwardVersion
Proxy&n endp
endm
FORWARD 0
FORWARD 1
FORWARD 2
FORWARD 3
FORWARD 4
FORWARD 5
FORWARD 6
FORWARD 7
FORWARD 8
FORWARD 9
FORWARD 10
FORWARD 11
FORWARD 12
FORWARD 13
FORWARD 14
FORWARD 15
FORWARD 16
end
