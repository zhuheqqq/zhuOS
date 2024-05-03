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
#include "../lib/stdio.h"
#include "memory.h"
#include "fs.h"

void k_thread_a(void*);
void k_thread_b(void*);
void u_prog_a(void);
void u_prog_b(void);
//int prog_a_pid = 0,prog_b_pid  = 0;

int main(void){
    put_str("hello kernel!\n");
    init_all();
    
    //intr_enable();

  
    process_execute(u_prog_a, "u_prog_a");
 
    process_execute(u_prog_b, "u_prog_b");
    
    thread_start("k_thread_a",32,k_thread_a,"argA  ");
   
    thread_start("k_thread_b",32,k_thread_b,"argB  ");
  

   printf("/dir1/subdir1 create %s!\n", sys_mkdir("/dir1/subdir1") == 0 ? "done" : "fail");
   printf("/dir1 create %s!\n", sys_mkdir("/dir1") == 0 ? "done" : "fail");
   printf("now, /dir1/subdir1 create %s!\n", sys_mkdir("/dir1/subdir1") == 0 ? "done" : "fail");
   int fd = sys_open("/dir1/subdir1/file2", O_CREAT|O_RDWR);
   if (fd != -1) {
      printf("/dir1/subdir1/file2 create done!\n");
      sys_write(fd, "Catch me if you can!\n", 21);
      sys_lseek(fd, 0, SEEK_SET);
      char buf[32] = {0};
      sys_read(fd, buf, 21); 
      printf("/dir1/subdir1/file2 says:\n%s", buf);
      sys_close(fd);
   }
    while(1);

    return 0;
}

void k_thread_a(void* arg){//void表示通用函数,被调用的函数知道自己需要什么类型的参数
    
    while(1);

}

void k_thread_b(void* arg){//void表示通用函数,被调用的函数知道自己需要什么类型的参数
    
    while(1);
}

//测试用户进程
void u_prog_a(void) {
   
    while(1);
}

void u_prog_b(void) {
    
    while(1);
}
