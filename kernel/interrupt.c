#include "interrupt.h"
#include "stdint.h"
#include "global.h"
#include "io.h"
#include "print.h"

#define PIC_M_CTRL 0x20 //主片的控制端口
#define PIC_M_DATA 0x21 //主片的数据端口
#define PIC_S_CTRL 0xa0 //从片的控制端口
#define PIC_S_DATA 0xa1 //从片的数据端口

#define IDT_DESC_CNT 0x81   //总共支持的中断数,0x80是系统调用的中断向量

#define EFLAGS_IF 0x00000200 //eflags寄存器的if位为1
#define GET_EFLAGS(EFLAG_VAR) asm volatile("pushfl;popl %0":"=g"(EFLAG_VAR))


char* intr_name[IDT_DESC_CNT];//保存异常的名字
intr_handler idt_table[IDT_DESC_CNT];

//定义中断处理程序数组，在kernel.S中定义的intrXXentry只是中断处理程序的入口，最终调用的是ide_table的处理程序

extern intr_handler intr_entry_table[IDT_DESC_CNT];//声明引用定义在kernel.S中的中断处理函数入口数组
extern uint32_t syscall_handler(void);


//中断门描述符结构体
struct gate_desc{
    uint16_t func_offset_low_word;
    uint16_t selector;
    uint8_t dcount; //双字计数段，是门描述符的第4字节

    uint8_t attribute;
    uint16_t func_offset_high_word;
};

//静态函数声明
static void make_idt_desc(struct gate_desc* p_gdesc,uint8_t attr,intr_handler function);
static struct gate_desc idt[IDT_DESC_CNT];  //中断门描述符数组

extern intr_handler intr_entry_table[IDT_DESC_CNT];//声明引用定义在kernel.S的中断处理函数入口数组

//初始化可编程中断控制器8259A
static void pic_init(void){
    //初始化主片
    outb(PIC_M_CTRL,0x11);//ICW1:边沿触发，级联8259,需要ICW4
    outb(PIC_M_DATA,0x20);//ICW2:其实中断向量号为0X20

    outb(PIC_M_DATA,0x04);//ICW3：IR2接从片
    outb(PIC_M_DATA,0x01);//ICW4：8086模式，正常EOI

    //初始化从片
    outb(PIC_S_CTRL,0x11);
    outb(PIC_S_DATA,0x28);//起始中断向量号0x28

    outb(PIC_S_DATA,0x02);
    outb(PIC_S_DATA,0x01);

    //打开主片上IRO，只接受时钟产生的中断
    //outb(PIC_M_DATA,0xfe);
    //outb(PIC_S_DATA,0xff);

    //测试键盘，只打开时钟和键盘中断
    outb(PIC_M_DATA,0xf8);
    outb(PIC_S_DATA,0xbf);

    put_str("pic_init done\n");
}

//创建中断门描述符
static void make_idt_desc(struct gate_desc* p_gdesc,uint8_t attr,intr_handler function){
    p_gdesc->func_offset_low_word=(uint32_t)function & 0x0000FFFF;
    p_gdesc->selector=SELECTOR_K_CODE;
    p_gdesc->dcount=0;
    p_gdesc->attribute=attr;
    p_gdesc->func_offset_high_word=((uint32_t)function&0xFFFF0000)>>16;
}

//初始化中断描述符表
static void idt_desc_init(void){
    int i,lastindex = IDT_DESC_CNT - 1;
    for(i=0;i<IDT_DESC_CNT;i++){//创建了IDT_DESC_CNT个中断描述符
        make_idt_desc(&idt[i],IDT_DESC_ATTR_DPL0,intr_entry_table[i]);
    }
    //单独处理系统调用，系统调用对应的中断门dpl为3,中断处理程序是单独的syscall_handler
    make_idt_desc(&idt[lastindex], IDT_DESC_ATTR_DPL3, syscall_handler);
    put_str("idt_desc_init done\n");
}

//通用的中断处理函数，一般用在异常出现时处理
static void general_intr_handler(uint8_t vec_nr){
    if(vec_nr==0x27||vec_nr==0x2f){
        return;//IRQ7和IRQ15会产生伪中断，不处理
    }

//将光标置为0,从屏幕左上角清出一片打印异常信息的区域
    set_cursor(0);
    int cursor_pos=0;
    while(cursor_pos<320){
        put_char(' ');
        cursor_pos++;
    }

    set_cursor(0);//设置光标值
    put_str("!!!!!!!!!!        excetion messqage begin        !!!!!!!!!!");
    set_cursor(88);
    put_str(intr_name[vec_nr]);
    if(vec_nr ==14){//如果为pagefault,将缺失的地址打印出来并悬停
        int page_fault_vaddr=0;
        asm("movl %%cr2,%0":"=r"(page_fault_vaddr));// cr2是存放造成page_fault的地址

        put_str("\n page fault addr is ");
        put_int(page_fault_vaddr);
    }

    put_str("\n !!!!!!!!!!        excetion message end        !!!!!!!!!!!");

    while(1);

}

//一般中断处理函数注册及异常名称注册
static void exception_init(void){
    int i;
    for(i=0;i<IDT_DESC_CNT;i++){
        idt_table[i]=general_intr_handler;
        intr_name[i]="unknown";
    }
    intr_name[0]="#DE Divide Error";
    intr_name[1]="#DE Debug Exception";
    intr_name[2]="NMI Interrupt";
    intr_name[3]="#BP Breakpoint Exception";
    intr_name[4]="#OF Overflow Exception";
    intr_name[5]="#BR BOUND Range Exceeded Exception";
    intr_name[6] = "#UD Invalid Opcode Exception";
    intr_name[7] = "#NM Device Not Available Exception";
    intr_name[8] = "#DF Double Fault Exception";
    intr_name[9] = "Coprocessor Segment Overrun";
    intr_name[10] = "#TS Invalid TSS Exception";
    intr_name[11] = "#NP Segment Not Present";
    intr_name[12] = "#SS Stack Fault Exception";
    intr_name[13] = "#GP General Protection Exception";
    intr_name[14] = "#PF Page-Fault Exception";
    // intr_name[15] 第15项是intel保留项，未使用
    intr_name[16] = "#MF x87 FPU Floating-Point Error";
    intr_name[17] = "#AC Alignment Check Exception";
    intr_name[18] = "#MC Machine-Check Exception";
    intr_name[19] = "#XF SIMD Floating-Point Exception";
}

/* 获取当前中断状态 */
enum intr_status intr_get_status() {
   uint32_t eflags = 0; 
   GET_EFLAGS(eflags);
   return (EFLAGS_IF & eflags) ? INTR_ON : INTR_OFF;
}


/* 开中断并返回开中断前的状态*/
enum intr_status intr_enable() {
   enum intr_status old_status;
   if (INTR_ON == intr_get_status()) {
      old_status = INTR_ON;
      return old_status;
   } else {
      old_status = INTR_OFF;
      asm volatile("sti");	 // 开中断,sti指令将IF位置1
      return old_status;
   }
}

/* 关中断,并且返回关中断前的状态 */
enum intr_status intr_disable() {     
   enum intr_status old_status;
   if (INTR_ON == intr_get_status()) {
      old_status = INTR_ON;
      asm volatile("cli" : : : "memory"); // 关中断,cli指令将IF位置0
                                          //cli指令不会直接影响内存。然而，从一个更大的上下文来看，禁用中断可能会影响系统状态，
                                          //这个状态可能会被存储在内存中。所以改变位填 "memory" 是为了安全起见，确保编译器在生成代码时考虑到这一点。
      return old_status;
   } else {
      old_status = INTR_OFF;
      return old_status;
   }
}

/* 将中断状态设置为status */
enum intr_status intr_set_status(enum intr_status status) {
   return status & INTR_ON ? intr_enable() : intr_disable();   //enable与disable函数会返回旧中断状态
}

/* 在中断处理程序数组第vector_no个元素中注册安装中断处理程序function */
void register_handler(uint8_t vector_no, intr_handler function) {
/* idt_table数组中的函数是在进入中断后根据中断向量号调用的,
 * 见kernel/kernel.S的call [idt_table + %1*4] */
   idt_table[vector_no] = function; 
}



/*完成有关中断的所有初始化工作*/
void idt_init() {
   put_str("idt_init start\n");
   idt_desc_init();	   //调用上面写好的函数完成中段描述符表的构建
   exception_init();	   // 异常名初始化并注册通常的中断处理函数
   pic_init();		  //设定化中断控制器，只接受来自时钟中断的信号

   /* 加载idt */
   uint64_t idt_operand = ((sizeof(idt) - 1) | ((uint64_t)(uint32_t)idt << 16));    //定义要加载到IDTR寄存器中的值
   asm volatile("lidt %0" : : "m" (idt_operand));
   put_str("idt_init done\n");
}