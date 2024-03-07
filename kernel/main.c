#include "../lib/kernel/print.h"
#include "init.h"
void main(void){
    put_str("I am kernel1\n");
    init_all();
    asm volatile("sti");
    while(1);
}
