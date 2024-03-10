#include "init.h"
#include "../lib/kernel/print.h"
#include "interrupt.h"
#include "../device/timer.h"

//初始化所有模块
void init_all(){
    put_str("init_all\n");
    idt_init();
    timer_init();
}