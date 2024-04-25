#include "stdio.h"
#include "interrupt.h"
#include "global.h"
#include "string.h"
#include "syscall.h"
#include "print.h"

#define va_start(ap, v) ap = (va_list)&v  // 把ap指向第一个固定参数v
#define va_arg(ap, t) *((t*)(ap += 4))	  // ap指向下一个参数并返回其值
#define va_end(ap) ap = NULL		  // 清除ap


//将整形转换成字符
static void itoa(uint32_t value, char** buf_ptr_addr, uint8_t base) {
    uint32_t m = value % base;  //最先掉下最低位
    uint32_t i = value / base; //取整
    if(i) {
        itoa(i, buf_ptr_addr, base);
    }
    if(m < 10) {
        *((*buf_ptr_addr)++) = m + '0';
    }else {
        *((*buf_ptr_addr)++) = m - 10 + 'A';
    }
}

//将参数ap按照格式输出到字符串str并返回替换后str长度
uint32_t vsprintf(char* str, const char* format, va_list ap) {
    char* buf_ptr = str;
    const char* index_ptr = format;
    char index_char = *index_ptr;   //用index_char找%
    int32_t arg_int;
    while(index_char) {
        if(index_char != '%') {
            *(buf_ptr++) = index_char;
            index_char = *(++index_ptr);
            continue;
        }
        index_char = *(++index_ptr);    //得到%后面的占位符
        switch(index_char) {
            case 'x':
                arg_int = va_arg(ap, int);
                itoa(arg_int, &buf_ptr, 16);
                index_char = *(++index_ptr);
                break;
        }
    }
    return strlen(str);
}

uint32_t printf(const char* format, ...) {
    va_list args;
    va_start(args, format); //使args指向format
    char buf[1024] = {0};   //储存拼接后的字符串
    vsprintf(buf, format, args);
    va_end(args);
    return write(buf);
}