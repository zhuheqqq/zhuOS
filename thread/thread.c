#include "thread.h"
#include "stdint.h"
#include "string.h"
#include "global.h"
#include "memory.h"
#include "interrupt.h"
#include "list.h"
#include "debug.h"
#include "print.h"
#include "process.h"
#include "sync.h"


#define PG_SIZE 4096

struct task_struct* idle_thread;//idle线程

struct task_struct* main_thread;//主线程pcb
struct list thread_ready_list;//就绪队列
struct list thread_all_list;//所有任务队列
static struct list_elem* thread_tag;//用于保存队列中的线程节点
struct lock pid_lock;//分配pid锁

extern void switch_to(struct task_struct* cur,struct task_struct* next);
extern void init(void);

//系统空闲时运行的线程
static void idle(void* arg UNUSED) {
    while(1) {
        thread_block(TASK_BLOCKED);
        //执行hlt时必须要保证目前处在开中断的情况下
        asm volatile("sti; hlt":::"memory");//先开中断，执行hlt挂起
    }
}

//获取当前线程pcb指针
struct task_struct* running_thread(){
    uint32_t esp;
    asm("mov %%esp, %0":"=g"(esp));

    //取esp整数部分，即pcb起始地址
    return (struct task_struct*)(esp&0xfffff000);
}

//由kernel_thread去执行function
static void kernel_thread(thread_func* function,void* func_arg){
    //执行function前要开中断，避免后面的时钟中断被屏蔽，而无法调度其他线程
    intr_enable();
    function(func_arg);
}

//分配pid
static pid_t allocate_pid(void) {
    static pid_t next_pid = 0;
    lock_acquire(&pid_lock);
    next_pid++;
    lock_release(&pid_lock);
    return next_pid;
}

//初始化线程栈
void thread_create(struct task_struct* pthread,thread_func function,void* func_arg){
    pthread->self_kstack-=sizeof(struct intr_stack);//预留中断使用栈的空间


    //留出线程栈的空间
    pthread->self_kstack-=sizeof(struct thread_stack);
    struct thread_stack* kthread_stack=(struct thread_stack*)pthread->self_kstack;
    kthread_stack->eip=kernel_thread;
    kthread_stack->function=function;
    kthread_stack->func_arg=func_arg;
    kthread_stack->ebp=kthread_stack->ebx=kthread_stack->esi=kthread_stack->edi=0;
}

//初始化线程基本信息
void init_thread(struct task_struct* pthread,char* name,int prio){
    memset(pthread,0,sizeof(*pthread));
    strcpy(pthread->name,name);
    pthread->pid = allocate_pid();

    if(pthread==main_thread){
        //由于把main函数也封装成一个线程，并且他一直是运行的
        pthread->status=TASK_RUNNING;
    }else{
        pthread->status=TASK_READY;
    }

    //预留标准输入输出
    pthread->fd_table[0] = 0;//标准输入
    pthread->fd_table[1] = 1;//标准输出
    pthread->fd_table[2] = 2;//标准错误
    //其余全部置1
    uint8_t fd_idx = 3;
    while(fd_idx < MAX_FILES_OPEN_PER_PROC) {
        pthread->fd_table[fd_idx] = -1;
        fd_idx++;
    }


    //self_kstack是线程自己在内核态下(特权级0)使用的栈顶地址
    pthread->self_kstack=(uint32_t*)((uint32_t)pthread+PG_SIZE);
    pthread->priority=prio;
    pthread->ticks=prio;
    pthread->elapsed_ticks=0;
    pthread->pgdir=NULL;
    pthread->stack_magic=0x19870916;//自定义魔数
    pthread->cwd_inode_nr = 0; //以根目录为默认工作路径
    pthread->parent_pid = -1;   //默认没有父进程

}

//创建一优先级为prio的线程，名为name,线程所执行函数function(func_arg)
struct task_struct* thread_start(char* name,int prio,thread_func function,void* func_arg){
    //pcb都位于内核空间
    struct task_struct* thread=get_kernel_pages(1); //先分配一页内存

    init_thread(thread,name,prio);
    thread_create(thread,function,func_arg);

    //确保之前不再队列中
    ASSERT(!elem_find(&thread_ready_list,&thread->general_tag));

    //加入就绪线程队列
    list_append(&thread_ready_list,&thread->general_tag);

    //确保之前不再队列中
    ASSERT(!elem_find(&thread_all_list,&thread->all_list_tag));
    list_append(&thread_all_list,&thread->all_list_tag);

    //asm volatile("movl %0,%%esp;pop %%ebp;pop %%ebx;pop %%edi;pop %%esi;ret"::"g"(thread->self_kstack):"memory");
    return thread; 
}

//将main完善为主线程
static void make_main_thread(void){
    /* 因为 main 线程早已运行,
* 咱们在 loader.S 中进入内核时的 mov esp,0xc009f000,
 * 就是为其预留 pcb 的,因此 pcb 地址为 0xc009e000,
* 不需要通过 get_kernel_page 另分配一页*/
    main_thread=running_thread();
    init_thread(main_thread,"main",32);

    //main函数是当前线程，当前线程不在thread_read_list中，所以只将其加在thread_all_list
    ASSERT(!elem_find(&thread_all_list,&main_thread->all_list_tag));
    list_append(&thread_all_list,&main_thread->all_list_tag);
}


//实现任务调度,将当前线程换下处理器，并在就绪队列中找个可运行的程序换上
void schedule(){
    ASSERT(intr_get_status()==INTR_OFF);

    struct task_struct* cur=running_thread();
    if(cur->status==TASK_RUNNING){
        //如果此线程是cpu时间片到了，将其加入就绪队列尾
        ASSERT(!elem_find(&thread_ready_list,&cur->general_tag));
        list_append(&thread_ready_list,&cur->general_tag);
        cur->ticks=cur->priority;

        cur->status=TASK_READY;
    }else{

    }

    if(list_empty(&thread_ready_list)) {
        thread_unblock(idle_thread);
    }

    ASSERT(!list_empty(&thread_ready_list));
    thread_tag=NULL;//清空thread_tag

    //将thread_ready_list队列中的第一个就绪线程弹出，准备将其调度上cpu
    thread_tag=list_pop(&thread_ready_list);
    struct task_struct* next=elem2entry(struct task_struct,general_tag,thread_tag);
    next->status=TASK_RUNNING;

    //激活任务页表等
    process_activate(next);

    switch_to(cur,next);
}

//线程将自己阻塞，标志其状态为stat
void thread_block(enum task_status stat){
    //stat取值为TASK_BLOCKED、TASK_WAITING、TASK_HANGINGA
    ASSERT(((stat==TASK_BLOCKED)||(stat==TASK_WAITING)||(stat==TASK_HANGING)));

    enum intr_status old_status=intr_disable();
    struct task_struct* cur_thread=running_thread();
    cur_thread->status=stat;//将状态置为stat

    //将当前线程换下处理器
    schedule();
    //当前线程被解除阻塞后才继续运行下面的intr_set_status
    intr_set_status(old_status);
}

//将线程解除阻塞
void thread_unblock(struct task_struct* pthread){
    enum intr_status old_status=intr_disable();
    ASSERT(((pthread->status==TASK_BLOCKED)||(pthread->status==TASK_HANGING)||(pthread->status==TASK_WAITING)));

    if(pthread->status!=TASK_READY){
        ASSERT(!elem_find(&thread_ready_list,&pthread->general_tag));

        if(elem_find(&thread_ready_list,&pthread->general_tag)){
            PANIC("thread_unblock:blocked thread in ready_list\n");
        }

        list_push(&thread_ready_list,&pthread->general_tag);//放到队列最前面使其可以尽快得到调度

        pthread->status=TASK_READY;
    }
    intr_set_status(old_status);
    
}

/*
先将当前任务重新加入到就绪队列
将当前任务status置为TASK_READY
最后调用schedule重新调度任务
*/
//主动让出cpu,换成其他线程运行
void thread_yield(void) {
    struct task_struct* cur = running_thread();
    enum intr_status old_status = intr_disable();//关中断，保证原子操作
    ASSERT(!elem_find(&thread_ready_list, &cur->general_tag));
    list_append(&thread_ready_list, &cur->general_tag);
    cur->status = TASK_READY;
    schedule();
    intr_set_status(old_status);
}

//简单的封装，使得能被外部函数调用
pid_t fork_pid(void) {
    return allocate_pid();
}

//初始化线程环境
void thread_init(void){
    put_str("thread_init start\n");
    list_init(&thread_ready_list);
    list_init(&thread_all_list);
    lock_init(&pid_lock);

    //创建第一个用户进程init
    process_execute(init, "init");


    //将当前main函数创建为线程
    make_main_thread();

    //创建idle线程
    idle_thread = thread_start("idle", 10, idle, NULL);
    put_str("thread_init done\n");
}