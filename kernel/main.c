#include "print.h"
#include "init.h"
#include "debug.h"
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
#include "shell.h"


int main(void){
    put_str("hello kernel!\n");
    init_all();
    cls_screen();
    console_put_str("[zhuheqin@localhost /] $ ");
    
    while(1);

    return 0;
}

void init(void) {
    uint32_t ret_pid = fork();
    if(ret_pid) {
        while(1);
    }else {
        my_shell();
    }
    PANIC("init: should not be here");
}