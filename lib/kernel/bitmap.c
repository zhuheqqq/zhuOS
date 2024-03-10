#include "bitmap.h"
#include "stdint.h"
#include "string.h"
#include "print.h"
#include "interrupt.h"
#include "debug.h"

//将位图初始化
void bitmap_init(struct bitmap* btmp){
    memset(btmp->bits,0,btmp->btmp_bytes_len);
}

//判断bit_idx位是否为1
bool bitmap_scan_test(struct bitmap* btmp,uint32_t bit_idx){
    uint32_t byte_idx=bit_idx/8;//向下取整用于索引数组下标

//在位图中，每个字节可以存储8个位
//所以要确定所在的字节，需要将位索引除以8（向下取整），得到所在字节的索引。

    uint32_t bit_odd=bit_idx%8;//取余用于索引数组的位
    return (btmp->bits[byte_idx]&(BITMAP_MASK<<bit_odd))?1:0;
}

//在位图中申请连续cnt个位，成功返回起始下标，失败返回-1
int bitmap_scan(struct bitmap* btmp,uint32_t cnt){
    //用于记录空闲位所在的字节
    uint32_t idx_byte=0;
    while((0xff==btmp->bits[idx_byte])&&(idx_byte<btmp->btmp_bytes_len)){
        idx_byte++;
    }

    ASSERT(idx_byte<btmp->btmp_bytes_len);
    if(idx_byte==btmp->btmp_bytes_len){//idx_byte已经遍历完整个位图
        return -1;
    }

    //在位图中某字节找到了空闲位，在该字节内逐位比对，返回空闲位的索引
    int idx_bit=0;

    //通过逻辑运算之后如果该位为1则被占用
    while((uint8_t)(BITMAP_MASK<<idx_bit)&btmp->bits[idx_byte]){
        idx_bit++;
    }

    int bit_idx_start=idx_byte*8+idx_bit;//空闲位在位图的下标
    if(cnt==1){
        return bit_idx_start;
    }

    uint32_t bit_left=(btmp->btmp_bytes_len*8-bit_idx_start);
    //记录还有多少位可以判断
    uint32_t next_bit=bit_idx_start+1;
    uint32_t count=1;//用于记录找到的空闲位的个数

    bit_idx_start=-1;//将其置-1,找不到连续的位就返回
    while(bit_left-->0){
        if(!(bitmap_scan_test(btmp,next_bit))){
            count++;
        }else{
            count=0;
        }

        if(count==cnt){
            bit_idx_start=next_bit-cnt+1;
            break;
        }
        next_bit++;
    }

    return bit_idx_start;

}

void bitmap_set(struct bitmap* btmp,uint32_t bit_idx,int8_t value){
    ASSERT((value==0)||(value==1));
    uint32_t byte_idx=bit_idx/8;
    uint32_t bit_odd=bit_idx%8;

    /* 一般都会用个 0x1 这样的数对字节中的位操作
    将 1 任意移动后再取反,或者先取反再移位,可用来对位置 0 操作。*/

    if(value){
        btmp->bits[byte_idx]|=(BITMAP_MASK<<bit_odd);
    }else{
        btmp->bits[byte_idx]&=~(BITMAP_MASK<<bit_odd);
    }
}

