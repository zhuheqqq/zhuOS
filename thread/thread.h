#ifndef __THREAD_THREAD_H
#define __THREAD_THREAD_H
#include "stdint.h"

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
    uint32_t stack_magic;       //栈的边界标记，用于检测栈的溢出
};

#endif