[bits 32]
%define  ERROR_CODE nop

%define ZERO push 0

extern put_str      ;声明外部函数
extern idt_table ;c中的注册的中断处理程序数组

section .data
global intr_entry_table         ;全局数组
intr_entry_table:

%macro VECTOR 2

section .text       ;代码范围的起始定义

;中断程序起始地址
intr%1entry:      ;每个中断处理程序都要压入中断向量号，所以每个中断类型自己知道自己的中断向量号

    %2
;保存上下文环境
    push ds
    push es
    push fs
    push gs
    pushad

    ;如果是从片上的中断，要往主片和从片都发送EOI
    mov al,0x20         ;中断结束命令EOI
    out 0xa0,al         ;向从片发送
    out 0x20,al         ;向主片发送

    push %1

    call [idt_table+%1*4];调用c的中断处理函数
    jmp intr_exit

section .data   
    dd intr%1entry      ;存储各个中断入口程序地址，形成intr_entry_table数组

%endmacro

section .text
global intr_exit
intr_exit:
;恢复上下文环境
    add esp,4
    popad
    pop gs
    pop fs
    pop es
    pop ds
    add esp,4   ;跳过error_code
    iretd


VECTOR 0x00,ZERO
VECTOR 0x01,ZERO
VECTOR 0x02,ZERO
VECTOR 0x03,ZERO
VECTOR 0x04,ZERO
VECTOR 0x05,ZERO
VECTOR 0x06,ZERO
VECTOR 0x07,ZERO
VECTOR 0x08,ERROR_CODE
VECTOR 0x09,ZERO
VECTOR 0x0a,ERROR_CODE
VECTOR 0x0b,ERROR_CODE
VECTOR 0x0c,ZERO
VECTOR 0x0d,ERROR_CODE
VECTOR 0x0e,ERROR_CODE
VECTOR 0x0f,ZERO
VECTOR 0x10,ZERO
VECTOR 0x11,ERROR_CODE
VECTOR 0x12,ZERO
VECTOR 0x13,ZERO
VECTOR 0x14,ZERO
VECTOR 0x15,ZERO
VECTOR 0x16,ZERO
VECTOR 0x17,ZERO
VECTOR 0x18,ERROR_CODE
VECTOR 0x19,ZERO
VECTOR 0x1a,ERROR_CODE
VECTOR 0x1b,ERROR_CODE
VECTOR 0x1c,ZERO
VECTOR 0x1d,ERROR_CODE
VECTOR 0x1e,ERROR_CODE    ;包含错误码的中断
VECTOR 0x1f,ZERO
VECTOR 0x20,ZERO