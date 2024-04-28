#include "init.h"
#include "print.h"
#include "interrupt.h"
#include "timer.h"
#include "memory.h"
#include "thread.h"
#include "console.h"
#include "keyboard.h"
#include "tss.h"
#include "syscall.h"
#include "syscall-init.h"
#include "ide.h"
#include "fs.h"

//初始化所有模块
void init_all(){
    put_str("init_all\n");
    idt_init();  //初始化中断
    timer_init(); //初始化PIT
    mem_init(); //初始化内存管理系统
    thread_init();//初始化线程相关结构
    console_init();//终端初始化
    keyboard_init();//键盘初始化
    tss_init();//tss初始化
    syscall_init();
    ide_init();
    filesys_init();
}