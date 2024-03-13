#include "../lib/kernel/print.h"
#include "init.h"
//#include "debug.h"
#include "../thread/thread.h"

void k_thread_a(void*);

int main(void){
    put_str("hello kernel!\n");
    init_all();

    thread_start("k_thread_a",31,k_thread_a,"argA ");


    while(1);

    return 0;
}

void k_thread_a(void* arg){//void表示通用函数,被调用的函数知道自己需要什么类型的参数
    char* para=arg;
    while(1){
        put_str(para);
    }

}
