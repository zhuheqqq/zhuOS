%include "boot.inc"
section loader vstart=LOADER_BASE_ADDR
LOADER_STACK_TOP equ LOADER_BASE_ADDR
jmp loader_start

;构建gdt及其内部的描述符
GDT_BASE: dd 0x00000000
            dd 0x00000000

CODE_DESC: dd 0x0000FFFF
            dd DESC_CODE_HIGH4

DATA_STACK_DESC: dd 0x0000
                    dd DESC_DATA_HIGH4

VIDEO_DESC: dd 0x80000 007;limit=(0xbffff-0xb8000)/4k=0x7
                dd DESC_VIDEO_HIGH4;此时dpl为0

GDT_SIZE equ $-GDT_BASE
GDT_LIMIT equ GDT_SIZE-1
times 60 dq 0               ;此处预留60个描述符的空位
SELECTOR_CODE equ (0x0001<<3)+TI_GDT+RPL0           ;相当于(CODE_DESC-GDT_BASE)/8+TI_GDT+RPL0
SELECTOR_DATA equ (0x0002<<3)+TI_GDT+RPL0
SECTION_VIDEO equ (0x0003<<3)+TI_GDT+RPL0

;下面是GDT的指针，前2个字节是GDT界限，后4个字节是GDT起始地址
gdt_ptr dw GDT_LIMIT
            dd GDT_BASE

    loadermsg db '2 loader in real'

    loader_start:

;-------------------------------------------------------------
;INT 0x10   功能号：0x13    功能描述：打印字符串
;-------------------------------------------------------------
mov sp,LOADER_BASE_ADDR
mov bp,loadermsg    ;ES:BP=字符串地址
mov cx,17   ;CX=字符串长度
mov ax,0x1301 ;AH=13,al=01h
mov bx,0x001f ;页号为0（BH=0）蓝底粉字（BL=1fh）
mov dx,0x1800
int 0x10   ;10h号中断

;-----------------------------------------准备进入保护模式---------------------------
;打开A20
;加载gdt
;将cr0的pe位置置1

;----------------------------------------打开A20------------------------------------------
in al,0x92
or al,0000_0010B
out 0x92,al

;------------------------------------------加载gdt-----------------------------------------
lgdt [gdt_ptr]

;------------------------------------------cr0第0位置1-------------------------------
mov eax,cr0
or eax,0x00000001
mov cr0,eax

jmp dword SELECTOR_CODE:p_mode_start;刷新流水线

[bits 32]
p_mode_start:
    mov ax,SELECTOR_DATA
    mov ds,ax
    mov es,ax
    mov ss,ax
    mov esp,LOADER_STACK_TOP
    mov ax,SECTION_VIDEO
    mov gs,ax

    mov byte [gs:160],'P'

    jmp $