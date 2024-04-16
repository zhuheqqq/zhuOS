#include "process.h"
#include "global.h"
#include "debug.h"
#include "memory.h"
#include "thread.h"    
#include "list.h"    
#include "tss.h"    
#include "interrupt.h"
#include "string.h"
#include "console.h"

extern void intr_exit(void);

//构建用户进程初始上下文
void start_process(void* filename_){
    void* function = filename_;//用户程序的名称
    struct task_struct* cur = running_thread();//获取pcb指针
    cur->self_kstack += sizeof(struct thread_stack);
    struct intr_stack* proc_stack = (struct intr_stack*)cur->self_kstack;

    proc_stack->edi = proc_stack->esi = proc_stack->ebp = proc_stack->esp_dummy = 0;
    proc_stack->ebx = proc_stack->edx = proc_stack->ecx = proc_stack->eax = 0;

    proc_stack->gs = 0; //用户态用不上，不允许用户进程访问显存，直接初始化为0
    proc_stack->ds = proc_stack->es = proc_stack->fs = SELECTOR_U_DATA;
    proc_stack->eip = function;    //待执行的用户程序地址
    proc_stack->cs = SELECTOR_U_CODE;//将栈中代码段寄存器cs赋值先前已在GDT中安装好的用户级代码段
    proc_stack->eflags = (EFLAGS_IOPL_0 | EFLAGS_MBS | EFLAGS_IF_1);
    proc_stack->esp = (void*)((uint32_t)get_a_page(PF_USER, USER_STACK3_VADDR) + PG_SIZE);//获取特权级3栈的下边界地址
    proc_stack->ss = SELECTOR_U_DATA;

//将栈 esp 替换为我们刚刚填充好的 proc_stack,然后通过 jmp intr_exit 使程序
//流程跳转到中断出口地址 intr_exit,通过那里的一系列 pop 指令和 iretd 指令,将 proc_stack 中的数据载入
//CPU 的寄存器,从而使程序“假装”退出中断,进入特权级 3。
    asm volatile("movl %0, %%esp; jmp intr_exit"::"g"(proc_stack):"memory");

}

//激活p_thread页表
void page_dir_activate(struct task_struct* p_thread) {
    /********************************************************
     * 执行此函数时,当前任务可能是线程。
     * 之所以对线程也要重新安装页表, 原因是上一次被调度的可能是进程,
     * 否则不恢复页表的话,线程就会使用进程的页表了。
     ********************************************************/

    //若为内核线程，需要重新填充页表为0x100000
    uint32_t pagedir_phy_addr = 0x100000;
    //默认为内核的页目录物理地址，也就是内核线程所用的页目录表
    if(p_thread->pgdir != NULL){//用户态进程自己的页目录表
        pagedir_phy_addr = addr_v2p((uint32_t)p_thread->pgdir);
    }

    //更新页目录寄存器cr3,使新页表生效
    asm volatile("movl %0, %%cr3"::"r"(pagedir_phy_addr):"memory");

}

//激活线程或进程的页表，更新tss的esp0为进程的特权级0的栈
void process_activate(struct task_struct* p_thread){
    ASSERT(p_thread != NULL);

    //激活该进程或线程的页表
    page_dir_activate(p_thread);

    //内核线程特权级本身就是0,处理器进入中断时并不会从tss中获取0特权级栈地址，故不需要更新esp0
    if(p_thread->pgdir){
        //更新该进程esp0,用于此进程被中断时保留上下文
        update_tss_esp(p_thread);
    }
}