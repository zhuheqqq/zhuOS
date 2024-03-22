#ifndef __THREAD_THREAD_H
#define __THREAD_THREAD_H
#include "stdint.h"
#include "list.h"

typedef void thread_func(void*);

//线程状态
enum task_status{
    TASK_RUNNING,
    TASK_READY,
    TASK_BLOCKED,
    TASK_WAITING,
    TASK_HANGING,
    TASK_DIED
};


//中断栈，用于中断发生时保护程序（线程或者进程）的上下文环境
struct intr_stack{
    uint32_t vec_no;        //kernel.S 宏 VECTOR 中 push %1 压入的中断号
    uint32_t edi;
    uint32_t esi;
    uint32_t edp;
    uint32_t esp_dummy;

    uint32_t ebx;
    uint32_t edx;
    uint32_t ecx;
    uint32_t eax;
    uint32_t gs;
    uint32_t fs;
    uint32_t es;
    uint32_t ds;

//特权级从低特权级进入高特权级时压入
    uint32_t err_code;
    void (*eip)(void);
    uint32_t cs;
    uint32_t eflags;
    void* esp;
    uint32_t ss;
};

//线程栈,用于存储线程中待执行的函数
struct thread_stack{
    uint32_t ebp;
    uint32_t ebx;
    uint32_t edi;
    uint32_t esi;

    //线程第一次执行时，eip指向待调用的函数kernel_thread,其他时候eip是指向swith_to的返回地址
    void (*eip)(thread_func *func,void* func_arg);

    void (*unused_retaddr);     //参数 unused_ret 只为占位置充数为返回地址
    thread_func* function;      //由kernel_thread所调用的函数名
    void* func_arg;         //所调用的参数
};

//进程或线程的pcb,程序控制块
struct task_struct{
    uint32_t* self_kstack;      //各内核线程都用自己的内核栈
    enum task_status status;     
    uint8_t priority;           //线程优先级
    char name[16];              //记录线程的名字

    uint8_t ticks;              //每次在处理器上执行的时间滴答数
    //也就是我们所说的任务的时间片,每次时钟中断都会将当前任务的 ticks 减 1,当减到 0时就被换下处理器。

    uint32_t elapsed_ticks;//任务执行了多久,从开始到结束总的时间

    struct list_elem general_tag;//线程在一般队列中的结点

    struct list_elem all_list_tag;

    uint32_t* pgdir;        //进程自己页表的虚拟地址


    uint32_t stack_magic;       //栈的边界标记，用于检测栈的溢出
};

void thread_create(struct task_struct* pthread, thread_func function, void* func_arg);
void init_thread(struct task_struct* pthread, char* name, int prio);
struct task_struct* thread_start(char* name, int prio, thread_func function, void* func_arg);
struct task_struct* running_thread(void);
void schedule(void);
void thread_init(void);
void thread_block(enum task_status stat);
void thread_unblock(struct task_struct* pthread);

#endif