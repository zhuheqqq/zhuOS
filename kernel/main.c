#include "../lib/kernel/print.h"
#include "init.h"
//#include "debug.h"
#include "../thread/thread.h"
#include "../device/console.h"
#include "interrupt.h"
#include "ioqueue.h"
#include "keyboard.h"

void k_thread_a(void*);
void k_thread_b(void*);

int main(void){
    put_str("hello kernel!\n");
    init_all();

    thread_start("consumer_a",32,k_thread_a,"argA ");
    thread_start("consumer_b",32,k_thread_b,"argB ");

    intr_enable();//打开中断，使时钟中断起作用


    while(1);//{
        //console_put_str("Main ");
  //  };

    return 0;
}

void k_thread_a(void* arg){//void表示通用函数,被调用的函数知道自己需要什么类型的参数
    char* para=arg;
    while(1){
        enum intr_status old_status = intr_disable();
        if(!ioq_empty(&kbd_buf)) {
            console_put_str(para);
            char byte = ioq_getchar(&kbd_buf);
            console_put_char(byte);
        }
        intr_set_status(old_status);
        
    }

}

void k_thread_b(void* arg){//void表示通用函数,被调用的函数知道自己需要什么类型的参数
    char* para=arg;
    while(1){
        enum intr_status old_status = intr_disable();
        if(!ioq_empty(&kbd_buf)) {
            console_put_str(para);
            char byte = ioq_getchar(&kbd_buf);
            console_put_char(byte);
        }
        intr_set_status(old_status);
        
    }
}
