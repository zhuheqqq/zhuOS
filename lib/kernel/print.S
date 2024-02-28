TI_GDT equ  0
RPL0 equ 0
SELECTOR_VIDEO equ (0x0003<<3)+TI_GDT+RPL0

[bits 32]
section .text
;----------------------------put_char(直接写内存)--------------------------------
;把栈中的1个字符写入光标所在处
;--------------------------------------------------------------------
global put_char
put_char:
    pushad      ;备份32位寄存器环境
    mov ax,SELECTOR_VIDEO      ;为保证GS中为正确的视频段选择子，每次打印都为GS赋值
    mov gs,ax

;-----------------------------获取光标位置----------------------------
    ;先获得高8位
    mov dx,0x03d4   ;索引寄存器
    mov al,0x0e     ;用于提供光标位置的高8位
    out dx,al
    mov dx,0x03d5   ;通过读写数据的端口0x3d5来获得或设置光标位置
    in al,dx        ;得到了光标的高8位
    mov ah,al

    ;获取低8位
    mov dx,0x03d4
    mov al,0x0f
    out dx,al
    mov dx,0x03d5
    in al,dx

    ;将光标存入bx
    mov bx,ax
    ;在栈中获取待打印的字符
    mov ecx,[esp+36]        
    
    cmp cl,0xd
    jz .is_carriage_return
    cmp cl,0xa
    jz .is_line_feed

    cmp cl,0x8         ;backspace的asc码是8
    jz .is_backspace
    jmp .put_other



.is_backspace:     ;回退键

    dec bx
    shl bx,1        ;光标左移一位等于乘2

    mov byte [gs:bx],0x20   ;将待删除的字节补成0或者空格
    inc bx
    mov byte [gs:bx],0x07
    shr bx,1
    jmp .set_cursor


.put_other:
    shl bx,1
    
    mov [gs:bx],cl
    inc bx
    mov byte [gs:bx],0x07   ;字符属性
    shr bx,1        ;恢复老的光标值
    inc bx          ;下一个光标值
    cmp bx,2000      ;将下次打印字符的坐标和2000比较
    jl .set_cursor      ;如果光标值小于2000,表示未写到显存的最后，则去设置新的光标值；若超出，则换行处理


.is_line_feed:      ;换行符LF
.is_carriage_return:        ;回车符CR
;如果是CR,则把光标移到行首
    xor dx,dx
    mov ax,bx
    mov si,80

    div si

    sub bx,dx

.is_carriage_return_end:        ;回车符处理结束
    add bx,80
    cmp bx,2000
.is_line_feed_end:           ;换行符LF，将光标+80
    jl .set_cursor


;-----------------------------------实现滚屏------------------------------------
;滚屏的原理是将屏幕的第1～24行搬运到第0～23行，再将第24行用空格填充
.roll_screen:
    cld
    mov ecx,960         ;2000-80=1920个字符要搬运，共1920*2=3840字节，一次搬运4字节，共3840/4=960次

    mov esi,0xc00b80a0    ;第一行行首
    mov edi,0xc00b8000    ;第0行行首
    rep movsd

;将最后一行填充为空白
    mov ebx,3840
    mov ecx,80

.cls:
    mov word [gs:ebx],0x720    ;0x720是黑底白字的空格键
    add ebx,2
    loop .cls                   ;循环执行
    mov bx,1920                 ;将光标值重置为1920,最后一行的首字符

.set_cursor:
;设置高8位
    mov dx,0x03d4
    mov al,0x0e
    out dx,al
    mov dx,0x03d5
    mov al,bh
    out dx,al

;设置低8位
    mov dx,0x03d4
    mov al,0x0f
    out dx,al
    mov dx,0x03d5
    mov al,bl
    out dx,al

.put_char_done:
    popad
    ret




