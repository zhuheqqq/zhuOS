;主引导程序
;-------------------------------------------------------------------------------------

%include "boot.inc"
SECTION MBREE vstart=0x7c00
        mov ax,cs
        mov ds,ax
        mov es,ax
        mov ss,ax
        mov fs,ax
        mov sp,0x7c00
        mov ax,0xb800
        mov gs,ax   ;存入段基址

;清屏
;利用0x06号功能，上卷全部行则可清屏


;------------------------------------------------------------------------------------
;INT 0x10  功能号:0x06  功能描述:上卷窗口
;------------------------------------------------------------------------------------


;输入
;AH=功能号=0x06
;AL=上卷的行数(如果为0,表示全部)
;BH=上卷行属性
;(CL,CH)=窗口左下角的(X,Y)位置
;(DL,DH)=窗口右下角的(X,Y)位置
;无返回值
        mov ax,0600h
        mov bx,0700h
        mov cx,0    ;左下角：（0,0）
        mov dx,184fh   ;右下角：（80,25）
                    ;VGA文本模式下，一行只能容纳80个字符，共25行
                    ;下标从0开始，所以0x18=24,0x4f=79
        int 10h

        ;输出背景色绿色，前景色红色，并且跳动的字符串“1 MBR”
        ;字符高字节是属性，低字节是字符ASCII码
        mov byte [gs:0x00],'1'      ;往以gs为数据段基址，以0为偏移地址的内存中写入字符为1的ASCII码
        mov byte [gs:0x01],0xA4     ;A表示绿色背景色闪烁，4表示前景色为红色

        mov byte [gs:0x02],' '
        mov byte [gs:0x03],0xA4

        mov byte [gs:0x04],'M'
        mov byte [gs:0x05],0xA4

        mov byte [gs:0x06],'B'
        mov byte [gs:0x07],0xA4

        mov byte [gs:0x08],'R'
        mov byte [gs:0x09],0xA4

        mov eax,LOADER_START_SECTOR     ;起始扇区lba地址
        mov bx,LOADER_BASE_ADDR         ;写入的地址
        mov cx,1                        ;待读入的扇区数
        call rd_disk_m_16               

        jmp LOADER_BASE_ADDR

;--------------------------------------------------------------------------------------------
;读取硬盘n个扇区
rd_disk_m_16:
;--------------------------------------------------------------------------------------------

        mov esi,eax         ;备份eax
        mov di,cx           ;备份cx

;读写硬盘
;设置要读取的扇区数
        mov dx,0x1f2
        mov a1,c1
        out dx,a1           ;读取的扇区数

        mov eax,esi         ;恢复ax

;将LBA地址存入0x1f3~0x1f6

        ;LBA地址7～0位写入端口0x1f3
        mov dx,0x1f3
        out dx,a1

        ;LBA地址15～8位写入端口0x1f4
        mov c1,8
        shr eax,c1
        mov dx,0x1f4
        out dx,a1

        ;LBA地址23～16位写入端口0x1f5
        shr eax,c1
        mov dx,0x1f5
        out dx,a1

        shr eax,c1
        and a1,0x0f     ;lba第24～27位
        or a1,0xe0      ;设置7～4位为1110表示lba模式
        mov dx,0x1f6
        out dx,a1

;向0x1f7端口写入命令0x20
        mov dx,0x1f7
        mov a1,0x20
        out dx,a1

;检测硬盘状态
    .not_ready:
        ;同一端口，写时表示写入命令字，读时便是读入硬盘状态
        nop
        in a1,dx
        and a1,0x88     ;第4位为1表示硬盘控制器已准备好数据传输
                        ;第7位为1表示硬盘忙
        cmp a1,0x08
        jnz .not_ready  ;没有准备好，继续等

;从0x1f0端口读数据
        mov ax,di
        mov dx,256
        mul dx
        mov cx,ax

        ;di为要读取的扇区数，一个扇区有512个字节，每次读入一个字，共需di*512/2次，所以di*256

        mov dx,0x1f0
    .go_on_read:
        in ax,dx
        mov [bx],ax
        add bx,2
        loop .go_on_read
        ret


        times 510-($-$$) db 0       ;凑够512字节
        db 0x55,0xaa