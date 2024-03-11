#include "../lib/kernel/print.h"
#include "init.h"
#include "debug.h"
int main(void){
    put_str("hello kernel!\n");
    init_all();


    while(1);

    return 0;
}
