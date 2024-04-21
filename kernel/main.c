#include "print.h"
#include "init.h"
//#include "debug.h"
#include "thread.h"
#include "console.h"
#include "interrupt.h"
#include "ioqueue.h"
#include "keyboard.h"
#include "process.h"
#include "syscall.h"
#include "syscall-init.h"
#include "stdio.h"

void k_thread_a(void*);
void k_thread_b(void*);
void u_prog_a(void);
void u_prog_b(void);
//int prog_a_pid = 0,prog_b_pid  = 0;

int main(void){
    put_str("hello kernel!\n");
    init_all();

    
    process_execute(u_prog_a,"user_prog_a");//创建了用户进程，u_prog_a是用户进程地址，是待运行的进程
    process_execute(u_prog_b,"user_prog_b");

    intr_enable();//打开中断，使时钟中断起作用
    console_put_str("main_pid:0x");
    console_put_int(sys_getpid());
    console_put_char('\n');
    thread_start("k_thread_a",32,k_thread_a,"argA ");
    thread_start("k_thread_b",32,k_thread_b,"argB ");


    while(1);//{
        //console_put_str("Main ");
  //  };

    return 0;
}

void k_thread_a(void* arg){//void表示通用函数,被调用的函数知道自己需要什么类型的参数
    char* para=arg;
    console_put_str("thread_a_pid:0x");
    console_put_int(sys_getpid());
    console_put_char('\n');
    // console_put_str("prog_a_pid:0x");
    // console_put_int(prog_a_pid);
    // console_put_char('\n');
    while(1);

}

void k_thread_b(void* arg){//void表示通用函数,被调用的函数知道自己需要什么类型的参数
    char* para=arg;
    console_put_str("thread_b_pid:0x");
    console_put_int(sys_getpid());
    console_put_char('\n');
    // console_put_str("prog_b_pid:0x");
    // console_put_int(prog_b_pid);
    // console_put_char('\n');
    while(1);
}

//测试用户进程
void u_prog_a(void) {
    printf("prog_a_pid:0x%x\n",getpid());
    while(1);
}

void u_prog_b(void) {
    printf("prog_b_pid:0x%x\n",getpid());
    while(1);
}
