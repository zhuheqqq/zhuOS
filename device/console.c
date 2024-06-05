//使终端输出更加简洁


#include "../lib/stdint.h"
#include "../thread/sync.h"
#include "../thread/thread.h"
#include "console.h"
#include "print.h"
static struct lock console_lock;//控制台锁即终端锁

//初始化终端
void console_init(){
    //put_str("console_init start\n");
    lock_init(&console_lock);
    //put_str("console_init done\n");
}

//获取终端
void console_acquire(){
    //put_str("console_acquire\n");
    lock_acquire(&console_lock);
}

//释放终端
void console_release(){
    //put_str("console_release\n");
    lock_release(&console_lock);
}

//终端中输出字符串
void console_put_str(char* str){
    console_acquire();
    //put_str("console_put_str\n");
    put_str(str);
    console_release();
}

//终端输出字符
void console_put_char(uint8_t char_asci){
    console_acquire();
    put_char(char_asci);
    console_release();
}

//终端输出十六进制整数
void console_put_int(uint32_t num){
    console_acquire();
    put_int(num);
    console_release();
}