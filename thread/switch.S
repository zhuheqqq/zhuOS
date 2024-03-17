[bits 32]
section .text
global switch_to

switch_to:
    ;栈中此处是返回地址
    push esi
    push edi
    push ebx
    push ebp

    mov eax,[esp+20]        ;得到栈中的参数cur=[esp+20],也就是当前线程的PCB地址
    mov [eax],esp           ;保存栈顶指针esp,task_struct的self_kstack字段


;--------------------------------------恢复一个线程环境----------------------------------
    mov eax,[esp+24]        ;得到栈中参数next=[esp+24]
    mov esp,[eax]   ;pcb第一个成员是self_kstack成员，用来记录0级栈顶指针，被换上cpu用来恢复0级栈
    ;0级栈中保存了进程或线程所有信息，包括3级栈指针

    pop ebp         ;按寄存器保存的逆顺序一次从栈中弹出
    pop ebx
    pop edi
    pop esi
    ret        ;返回switch_to的下面注释的返回地址，未由中断进入，第一次执行时会返回kernel_thread