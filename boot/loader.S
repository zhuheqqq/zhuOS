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

   mov byte [gs:160],'V'

   jmp $


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

   