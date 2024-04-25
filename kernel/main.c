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

    thread_start("k_thread_a",32,k_thread_a,"argA  ");
    thread_start("k_thread_b",32,k_thread_b,"argB  ");


    while(1);

    return 0;
}

void k_thread_a(void* arg){//void表示通用函数,被调用的函数知道自己需要什么类型的参数
    char* para = arg;
   void* addr1;
   void* addr2;
   void* addr3;
   void* addr4;
   void* addr5;
   void* addr6;
   void* addr7;
   console_put_str(" thread_a start\n");
   int max = 1000;
   while (max-- > 0) {
      int size = 128;
      addr1 = sys_malloc(size); 
      size *= 2; 
      addr2 = sys_malloc(size); 
      size *= 2; 
      addr3 = sys_malloc(size);
      sys_free(addr1);
      addr4 = sys_malloc(size);
      size *= 2; size *= 2; size *= 2; size *= 2; 
      size *= 2; size *= 2; size *= 2; 
      addr5 = sys_malloc(size);
      addr6 = sys_malloc(size);
      sys_free(addr5);
      size *= 2; 
      addr7 = sys_malloc(size);
      sys_free(addr6);
      sys_free(addr7);
      sys_free(addr2);
      sys_free(addr3);
      sys_free(addr4);
   }
   console_put_str(" thread_a end\n");
   while(1);

}

void k_thread_b(void* arg){//void表示通用函数,被调用的函数知道自己需要什么类型的参数
    char* para = arg;
   void* addr1;
   void* addr2;
   void* addr3;
   void* addr4;
   void* addr5;
   void* addr6;
   void* addr7;
   void* addr8;
   void* addr9;
   int max = 1000;
   console_put_str(" thread_b start\n");
   while (max-- > 0) {
      int size = 9;
      addr1 = sys_malloc(size);
      size *= 2; 
      addr2 = sys_malloc(size);
      size *= 2; 
      sys_free(addr2);
      addr3 = sys_malloc(size);
      sys_free(addr1);
      addr4 = sys_malloc(size);
      addr5 = sys_malloc(size);
      addr6 = sys_malloc(size);
      sys_free(addr5);
      size *= 2; 
      addr7 = sys_malloc(size);
      sys_free(addr6);
      sys_free(addr7);
      sys_free(addr3);
      sys_free(addr4);

      size *= 2; size *= 2; size *= 2; 
      addr1 = sys_malloc(size);
      addr2 = sys_malloc(size);
      addr3 = sys_malloc(size);
      addr4 = sys_malloc(size);
      addr5 = sys_malloc(size);
      addr6 = sys_malloc(size);
      addr7 = sys_malloc(size);
      addr8 = sys_malloc(size);
      addr9 = sys_malloc(size);
      sys_free(addr1);
      sys_free(addr2);
      sys_free(addr3);
      sys_free(addr4);
      sys_free(addr5);
      sys_free(addr6);
      sys_free(addr7);
      sys_free(addr8);
      sys_free(addr9);
   }
   console_put_str(" thread_b end\n");
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
