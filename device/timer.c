//8253可以给IRQ0引脚的时钟信号提速，使其中断信号频率快一些
/*
引脚的时钟中断信号由8253计数器0设置的，所以要使用计数器0
时钟信号必须周期性的发出，采用循环计数的方式
计数器的频率是由计数器初值决定的，所以要为计数器赋合适的计数初值
1193180/中断信号的频率=计数器0的初始计数值
*/

#include "timer.h"
#include "../lib/kernel/io.h"
#include "../lib/kernel/print.h"
#include "../thread/thread.h"
#include "../kernel/interrupt.h"
#include "../kernel/debug.h"

#define IRQ0_FREQUENCY 100

#define mil_seconds_per_intr (1000 / IRQ0_FREQUENCY)

#define IRQ0_FREQUENCY	   100
#define INPUT_FREQUENCY	   1193180
#define COUNTER0_VALUE	   INPUT_FREQUENCY / IRQ0_FREQUENCY
#define CONTRER0_PORT	   0x40
#define COUNTER0_NO	   0
#define COUNTER_MODE	   2
#define READ_WRITE_LATCH   3
#define PIT_CONTROL_PORT   0x43

uint32_t ticks;//自中断开启以来总共的滴答数

/* 把操作的计数器counter_no、读写锁属性rwl、计数器模式counter_mode写入模式控制寄存器并赋予初始值counter_value */
static void frequency_set(uint8_t counter_port, \
			  uint8_t counter_no, \
			  uint8_t rwl, \
			  uint8_t counter_mode, \
			  uint16_t counter_value) {
/* 往控制字寄存器端口0x43中写入控制字 */
   outb(PIT_CONTROL_PORT, (uint8_t)(counter_no << 6 | rwl << 4 | counter_mode << 1));
/* 先写入counter_value的低8位 */
   outb(counter_port, (uint8_t)counter_value);
/* 再写入counter_value的高8位 */
//直接右移会导致时钟频率过高出现GP异常
   //outb(counter_port, (uint8_t)counter_value >> 8);

   outb(counter_port, (uint8_t) (counter_value>>8) );
}


static void intr_timer_handler(void){
   struct task_struct* cur_thread=running_thread();//获取当前线程的pcb指针

   ASSERT(cur_thread->stack_magic==0x19870916);//检查栈是否溢出

   cur_thread->elapsed_ticks++;//记录线程占用的cpu时间
   ticks++;//从内核第一次处理时间中断开始至今的滴答数，内核态和用户态总共的滴答数

   if(cur_thread->ticks==0){//如果时间片用完，就调度新的进程上cpu
      schedule();
   }else{
      cur_thread->ticks--;//将当前时间片减1
   }
}

//以ticks为单位的sleep,任何时间形式的sleep会转换此ticks形式
static void ticks_to_sleep(uint32_t sleep_ticks) {
   uint32_t start_tick = ticks;

   //若间隔ticks不够就让出cpu
   while(ticks - start_tick < sleep_ticks) {
      thread_yield();
   }
}

//以毫秒为单位的sleep
void mtime_sleep(uint32_t m_seconds) {
   uint32_t sleep_ticks = DIV_ROUND_UP(m_seconds, mil_seconds_per_intr);
   ASSERT(sleep_ticks > 0);
   ticks_to_sleep(sleep_ticks);
}

/* 初始化PIT8253 */
void timer_init() {
   put_str("timer_init start\n");
   /* 设置8253的定时周期,也就是发中断的周期 */
   frequency_set(CONTRER0_PORT, COUNTER0_NO, READ_WRITE_LATCH, COUNTER_MODE, COUNTER0_VALUE);
   register_handler(0x20,intr_timer_handler);//注册时钟处理程序
   put_str("timer_init done\n");
}
