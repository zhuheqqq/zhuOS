%include "boot.inc"
section loader vstart=LOADER_BASE_ADDR
LOADER_STACK_TOP equ LOADER_BASE_ADDR
jmp loader_start

;构建gdt及其内部的描述符
GDT_BASE: dd 0x00000000
            dd 0x00000000

CODE_DESC: dd 0x0000FFFF
            dd DESC_CODE_HIGH4

DATA_STACK_DESC: dd 0x0000FFFF
                    dd DESC_DATA_HIGH4

VIDEO_DESC: dd 0x80000007   ;limit=(0xbffff-0xb8000)/4k=0x7
                dd DESC_VIDEO_HIGH4 ;此时dpl为0

GDT_SIZE equ $-GDT_BASE
GDT_LIMIT equ GDT_SIZE-1

;与书中不同，因为我们的gdt_base=0xc0000903，所以下面为
times 59 dq 0
times 5 db 0              


SELECTOR_CODE equ (0x0001<<3)+TI_GDT+RPL0           ;相当于(CODE_DESC-GDT_BASE)/8+TI_GDT+RPL0
SELECTOR_DATA equ (0x0002<<3)+TI_GDT+RPL0
SELECTOR_VIDEO equ (0x0003<<3)+TI_GDT+RPL0

;total_mem_bytes用于保存内存容量，以字节为单位，此位置比较好记
;当前偏移loader.bin文件头0x200字节
;loader.bin的加载地址是0x900
;故total_mem_bytes内存中的地址是0xb00
;将来在内核中咱们会引用此地址
total_mem_bytes dd 0
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;


;下面是GDT的指针，前2个字节是GDT界限，后4个字节是GDT起始地址
gdt_ptr dw GDT_LIMIT
            dd GDT_BASE

;人工对齐total_mem_bytes4+gdt_ptr6+ards_buf244+ards_nr2,共256字节
ards_buf times 244 db 0
ards_nr dw 0            ;用于记录ARDS结构体数量


loader_start:

;int 15h eax=000E820h,edx=534D4150h('SMAP')获取内存布局
        xor ebx,ebx         ;第一次调用时，ebx值要为0
        mov edx,0x534d4150   ;edx只赋值一次，在循环中不会改变
        mov di,ards_buf      ;ards结构体缓冲区
    .e820_mem_get_loop:
        mov eax,0x0000e820    ;执行int 0x15后，eax值变为0x534d4150,所以每次执行int前都要更新为子功能号
        mov ecx,20            ;ARDS地址范围描述符结构大小是20字节
        int 0x15
        jc .e820_failed_so_try_e801
    ;若cf位为1则有错误发生，尝试0xe801子功能
        add di,cx              ;使di增加20字节指向缓冲区新的ARDS结构位置
        inc word[ards_nr]       ;记录ARDS数量
        cmp ebx,0               ;若ebx为0且cf不为1,说明ards全部返回
        jnz .e820_mem_get_loop

    ;在所有ards结构中找出内存容量最大值
        mov cx,[ards_nr]
    ;遍历每一个ARDS结构体，循环次数是ards的数量
        mov ebx,ards_buf
        xor edx,edx              ;edx为最大内存容量，在此先清0
    .find_max_mem_area:
    ;无需判断type是否为1,最大的内存块一定是可被使用的
        mov eax,[ebx]
        add eax,[ebx+8]
        add ebx,20
        cmp edx,eax
    ;冒泡排序，找出最大，edx寄存器始终是最大的内存容量
        jge .next_ards
        mov edx,eax             ;edx为总内存大小
    .next_ards:
        loop .find_max_mem_area
        jmp .mem_get_ok


    .e820_failed_so_try_e801:
        mov ax,0xe801
        int 0x15
        jc .e801_failed_so_try88


        mov cx,0x400
        mul cx
        shl edx,16
        and eax,0x0000FFFF
        or edx,eax
        add edx,0x100000
        mov esi,edx

        xor eax,eax
        mov ax,bx
        mov ecx,0x10000
        mul ecx

        add esi,eax

        mov edx,esi
        jmp .mem_get_ok

    .e801_failed_so_try88:
        mov ah,0x88
        int 0x15
        jc .error_hlt
        and eax,0x0000FFFF

        mov cx,0x400
        mul cx
        shl edx,16
        or edx,eax
        add edx,0x100000

    .mem_get_ok:
        mov [total_mem_bytes],edx



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

    .error_hlt:       ;出错则挂起
        hlt


[bits 32]
p_mode_start:
    mov ax,SELECTOR_DATA
    mov ds,ax
    mov es,ax
    mov ss,ax
    mov esp,LOADER_STACK_TOP
    

mov eax,KERNEL_START_SECTOR ;kernel.bin所在扇区号
mov ebx,KERNEL_BIN_BASE_ADDR;从磁盘读出后，写入到ebx指定的地址
mov ecx,200                 ;读入的扇区数

call rd_disk_m_32   ;从硬盘读取文件
 

;创建页目录及页表并初始化页内存位图
   call setup_page
;要将描述符表地址及偏移量写入内存gdt_ptr，一会儿用新地址重新加载
   sgdt [gdt_ptr]              ;存储到原来gdt所在的位置

;将gdt描述符中视频段描述符段基址+0x0000000
   mov ebx,[gdt_ptr+2]              
   or dword [ebx+0x18+4],0xc0000000

   add dword [gdt_ptr+2],0xc0000000
   add esp,0xc0000000

;把页目录地址赋给cr3
   mov eax,PAGE_DIR_TABLE_POS
   mov cr3,eax

;打开cr0的pg位
   mov eax,cr0
   or eax,0x80000000
   mov cr0,eax

;开启分页后用gdt新的地址重新加载
   lgdt [gdt_ptr]

   mov eax,SELECTOR_VIDEO
   mov gs,eax

   jmp SELECTOR_CODE:enter_kernel

;-----------------------跳转到kernel-------------------------
enter_kernel:
    call kernel_init
    mov esp,0xc009f000
    jmp KERNEL_ENTER_ADDR


;------------------- 建立页表 -------------------
setup_page:
   mov ecx,4096
   mov esi,0            ;将页目录占用空间清0
.clear_page_dir:
   mov byte [PAGE_DIR_TABLE_POS+esi],0
   inc esi
   loop .clear_page_dir

;创建页目录项(PDE)
.create_pde:
   mov eax,PAGE_DIR_TABLE_POS
   add eax,0x1000               ;此时的eax为第一个页表的物理地址
   mov ebx,eax                  ;ebx=eax，为后续的.create_pte做准备，ebx为基址

;下面将偏移地址0x0（第1个）和0xc00（第768个页目录项）存为第1个页表的地址，每个页表表示4MB内存
   or eax,PG_US_U|PG_RW_W|PG_P        ;最低特权级|可读写|存在
   mov [PAGE_DIR_TABLE_POS+0x0],eax   ;第1个页目录项
   mov [PAGE_DIR_TABLE_POS+0xc00],eax ;第768个页目录项
   sub eax,0x1000
   mov [PAGE_DIR_TABLE_POS+4092],eax  ;最后一个页目录项指向页目录自己

;创建页表项(PTE)
   mov ecx,256                  ;对低端内存1MB建页表：1MB/4KB=256（256个页表项，1个页表足矣）
   mov esi,0
   mov edx,PG_US_U|PG_RW_W|PG_P ;最低特权第|可读写|存在
.create_pte:
   mov [ebx+esi*4],edx          ;逐个页表项设置
   add edx,4096                 ;因为1个页表4KB，所以edx的基址+4KB
   inc esi
   loop .create_pte

;创建内核其它页表的PDE
   mov eax,PAGE_DIR_TABLE_POS
   add eax,0x2000               ;此时的eax为第二个页表的物理地址
   or eax,PG_US_U|PG_RW_W|PG_P  ;最低特权级|可读写|存在
   mov ebx,PAGE_DIR_TABLE_POS
   mov ecx,254
   mov esi,769
.create_kernel_pde:
   mov [ebx+esi*4],eax          ;将第2个~第256个页表的地址逐个存入页表项
   inc esi
   add eax,0x1000               ;下一个页表的地址
   loop .create_kernel_pde

   ret

;------ 初始化内核 把缓冲区的内核代码放到0x1500区域 ------
kernel_init:
   xor eax,eax
   xor ebx,ebx ;ebx记录程序头表地址
   xor ecx,ecx ;cx记录程序头表中的program header数量
   xor edx,edx ;dx记录program header尺寸，即e_phentsize

   mov dx,[KERNEL_BIN_BASE_ADDR+42] ;偏移文件42字节处的属性是e_phentsize，表示program header大小
   mov ebx,[KERNEL_BIN_BASE_ADDR+28] ;偏移文件28字节处是e_phoff，表示第一个program>在文件中的偏移量
   add ebx,KERNEL_BIN_BASE_ADDR
   mov cx,[KERNEL_BIN_BASE_ADDR+44] ;偏移文件44字节处是e_phnum，表示有几个program header
.each_segment:
   cmp byte [ebx+0],PT_NULL         ;若p_type等于PT_NULL，说明此program header未使用
   je .PTNULL

   mov eax,[ebx+8]
   mov esi,0xc0001500
   cmp eax,esi
   jb .PTNULL

   ;为函数memcpy压入参数，参数是从右往左依次压入，函数原型类似于memcpy(dst,src,size)
   push dword [ebx+16]              ;偏移16字节的地方是p_filesz，压入函数memcpy的第三个参数：size
   mov eax,[ebx+4]                  ;偏移4字节的位置是p_offset
   add eax,KERNEL_BIN_BASE_ADDR     ;加上kernel.bin被加载到的物理地址，eax为该段的物理地址
   push eax                         ;压入函数memcpy的第二个参数：src
   push dword [ebx+8]               ;压入函数memcpy的第三个参数：dst，偏移量为8字节的位置是p_vaddr
   call mem_cpy                     ;调用memcpy完成段复制
   add esp,12                       ;清理栈中压入的三个参数
.PTNULL:
   add ebx,edx                      ;edx为program header大小，即e_phentsize，ebx指向下一个program_header
   loop .each_segment
   ret

mem_cpy:
   cld
   push ebp
   mov ebp,esp
   push ecx          ;rep指令需要ecx，但ecx还此时用于外循环中，所以先push保存一下
   mov edi,[ebp+8]
   mov esi,[ebp+12]
   mov ecx,[ebp+16]
   rep movsb         ;逐字节拷贝

   ;恢复
   pop ecx
   pop ebp
   ret

;------ rd_disk_m_32，类似于mbr.S中的rd_disk_m_16 ------
rd_disk_m_32:
   ;1 写入待操作磁盘数
   ;2 写入LBA 低24位寄存器 确认扇区
   ;3 device 寄存器 第4位主次盘 第6位LBA模式 改为1
   ;4 command 写指令
   ;5 读取status状态寄存器 判断是否完成工作
   ;6 完成工作 取出数据

   ;1 写入待操作磁盘数
   mov esi,eax   ; !!! 备份eax
   mov di,cx     ; !!! 备份cx

   mov dx,0x1F2  ; 0x1F2为Sector Count 端口号 送到dx寄存器中
   mov al,cl     ; !!! 忘了只能由ax al传递数据
   out dx,al     ; !!! 这里修改了 原out dx,cl

   mov eax,esi   ; !!!袄无! 原来备份是这个用 前面需要ax来传递数据 麻了

   ;2 写入LBA 24位寄存器 确认扇区
   mov cl,0x8    ; shr 右移8位 把24位给送到 LBA low mid high 寄存器中

   mov dx,0x1F3  ; LBA low
   out dx,al

   mov dx,0x1F4  ; LBA mid
   shr eax,cl    ; eax为32位 ax为16位 eax的低位字节 右移8位即8~15
   out dx,al

   mov dx,0x1F5
   shr eax,cl
   out dx,al

   ;3 device 寄存器 第4位主次盘 第6位LBA模式 改为1
             ; 24 25 26 27位 尽管我们知道ax只有2 但还是需要按规矩办事
             ; 把除了最后四位的其他位置设置成0
   shr eax,cl

   and al,0x0f
   or al,0xe0    ;!!! 把第四-七位设置成0111 转换为LBA模式
   mov dx,0x1F6  ; 参照硬盘控制器端口表 Device
   out dx,al

   ;4 向Command写操作 Status和Command一个寄存器
   mov dx,0x1F7  ; Status寄存器端口号
   mov ax,0x20   ; 0x20是读命令
   out dx,al

   ;5 向Status查看是否准备好惹
         ;设置不断读取重复 如果不为1则一直循环
.not_ready:
   nop           ; !!! 空跳转指令 在循环中达到延时目的
   in al,dx      ; 把寄存器中的信息返还出来
   and al,0x88   ; !!! 0100 0100 0x88
   cmp al,0x08
   jne .not_ready; !!! jump not equal == 0

   ;6 读取数据
   mov ax,di     ;把 di 储存的cx 取出来
   mov dx,256
   mul dx        ;与di 与 ax 做乘法 计算一共需要读多少次 方便作循环 低16位放ax 高16位放dx
   mov cx,ax     ;loop 与 cx相匹配 cx-- 当cx == 0即跳出循环
   mov dx,0x1F0
.go_read_loop:
   in ax,dx      ;两字节dx 一次读两字
   mov [ebx],ax
   add ebx,2
   loop .go_read_loop
   ret

   