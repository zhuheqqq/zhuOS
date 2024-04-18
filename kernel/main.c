#include "../lib/kernel/print.h"
#include "init.h"
//#include "debug.h"
#include "../thread/thread.h"
#include "../device/console.h"
#include "interrupt.h"
#include "../device/ioqueue.h"
#include "keyboard.h"
#include "../userprog/process.h"

void k_thread_a(void*);
void k_thread_b(void*);
void u_prog_a(void);
void u_prog_b(void);
int test_var_a = 0,test_var_b = 0;

int main(void){
    put_str("hello kernel!\n");
    init_all();

    thread_start("consumer_a",32,k_thread_a,"argA ");
    thread_start("consumer_b",32,k_thread_b,"argB ");
    process_execute(u_prog_a,"user_prog_a");//创建了用户进程，u_prog_a是用户进程地址，是待运行的进程
    process_execute(u_prog_b,"user_prog_b");

    intr_enable();//打开中断，使时钟中断起作用


    while(1);//{
        //console_put_str("Main ");
  //  };

    return 0;
}

void k_thread_a(void* arg){//void表示通用函数,被调用的函数知道自己需要什么类型的参数
    char* para=arg;
    while(1){
       /* enum intr_status old_status = intr_disable();
        if(!ioq_empty(&kbd_buf)) {
            console_put_str(para);
            char byte = ioq_getchar(&kbd_buf);
            console_put_char(byte);
        }
        intr_set_status(old_status);*/
        console_put_str("v_a:0x");
        console_put_int(test_var_a);
       // console_put_char('\n');
        
    }

}

void k_thread_b(void* arg){//void表示通用函数,被调用的函数知道自己需要什么类型的参数
    char* para=arg;
    while(1){
        /*enum intr_status old_status = intr_disable();
        if(!ioq_empty(&kbd_buf)) {
            console_put_str(para);
            char byte = ioq_getchar(&kbd_buf);
            console_put_char(byte);
        }
        intr_set_status(old_status);*/
        console_put_str("v_b:0x");
        console_put_int(test_var_b);
       // console_put_char('\n');
        
    }
}

//测试用户进程
void u_prog_a(void) {
    while(1){
        test_var_a++;
    }
}

void u_prog_b(void) {
    while(1){
        test_var_b++;
    }
}
