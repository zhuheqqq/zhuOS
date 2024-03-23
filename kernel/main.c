#include "../lib/kernel/print.h"
#include "init.h"
//#include "debug.h"
#include "../thread/thread.h"
#include "../device/console.h"
#include "interrupt.h"

void k_thread_a(void*);
void k_thread_b(void*);

int main(void){
    put_str("hello kernel!\n");
    init_all();

    thread_start("k_thread_a",32,k_thread_a,"argA ");
    thread_start("k_thread_b",8,k_thread_b,"argB ");

    intr_enable();//打开中断，使时钟中断起作用


    while(1){
        console_put_str("Main\n");
    }

    return 0;
}

void k_thread_a(void* arg){//void表示通用函数,被调用的函数知道自己需要什么类型的参数
    char* para=arg;
    while(1){
        console_put_str(para);
    }

}

void k_thread_b(void* arg){//void表示通用函数,被调用的函数知道自己需要什么类型的参数
    char* para=arg;
    while(1){
        console_put_str(para);
    }

}
