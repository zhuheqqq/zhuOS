//所有包含io.h的文件都会获得一份io.h中所有函数的拷贝
//这些函数对底层硬件端口直接操作，需要非常快速的响应




/**************	 机器模式   ***************
	 b -- 输出寄存器QImode名称,即寄存器中的最低8位:[a-d]l。
	 w -- 输出寄存器HImode名称,即寄存器中2个字节的部分,如[a-d]x。

	 HImode
	     “Half-Integer”模式，表示一个两字节的整数。 
	 QImode
	     “Quarter-Integer”模式，表示一个一字节的整数。 
*******************************************/ 

#ifndef __LIB_IO_H
#define __LIB_IO_H
#include "stdint.h"

//向端口port写入一个字节
static inline void outb(uint16_t port,uint8_t data){
    asm volatile("outb %b0,%w1"::"a"(data),"Nd"(port));
    /*asm volatile: 这个指令告诉编译器内嵌的汇编是"volatile"的，意味着编译器不会优化或重新排列这段汇编代码。

"outb %b0,%w1": 这是内嵌汇编的一部分，它是一个输出指令，用于向指定的I/O端口写入数据。outb 是一个x86架构的汇编指令，用于将数据写入指定的I/O端口。%b0 表示使用寄存器 %al 中的数据作为输出，%w1 表示使用内存中一个16位的值作为输出端口。

::: 这个符号用于分隔输入和输出列表。

"a"(data): 这是输入操作数列表的一部分。它告诉编译器将data的值放入汇编指令中的 %al 寄存器中。data是一个变量，存储了要写入端口的数据。

"Nd"(port): 这也是输入操作数列表的一部分。它告诉编译器将port的值放入汇编指令中的内存地址中。port是一个变量，存储了要写入的端口号。*/
}

/* 将addr处起始的word_cnt个字写入端口port */
static inline void outsw(uint16_t port,const void* addr,uint32_t word_cnt){
    /*********************************************************
   +表示此限制即做输入又做输出.
   outsw是把ds:esi处的16位的内容写入port端口, 我们在设置段描述符时, 
   已经将ds,es,ss段的选择子都设置为相同的值了,此时不用担心数据错乱。*/
   asm volatile ("cld; rep outsw" : "+S" (addr), "+c" (word_cnt) : "d" (port));
/******************************************************/
}

//将从端口port读入的一个字节返回
static inline uint8_t inb(uint16_t port){
    uint8_t data;
    asm volatile("inb %w1,%b0":"=a"(data):"Nd"(port));
    return data;
}

//将从端口port读入的word_cnt个字写入addr
static inline void insw(uint16_t port,void* addr,uint32_t word_cnt){
    /******************************************************
   insw是将从端口port处读入的16位内容写入es:edi指向的内存,
   我们在设置段描述符时, 已经将ds,es,ss段的选择子都设置为相同的值了,
   此时不用担心数据错乱。*/
   asm volatile ("cld; rep insw" : "+D" (addr), "+c" (word_cnt) : "d" (port) : "memory");
/******************************************************/
}

#endif