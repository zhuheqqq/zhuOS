#include "memory.h"
#include "stdint.h"
#include "print.h"

#define PG_SIZE 4096        //页的尺寸

/*----------------------------------------位图地址-----------------------------
0xc009f000是内核主线程栈顶，0x009e00是内核主线程的pcb
一个页框大小的位图可表示128mb内存，位图位置安排在地址0xc009a000
这样最多支持4个页框的位图，即512mb
*/

#define MEM_BITMAP_BASE 0xc009a000

#define K_HEAP_START 0xc0100000    //跨过低1mb内存，使虚拟地址在逻辑上连续

struct pool{
    struct bitmap pool_bitmap;//本内存池用到的位图结构，用于管理物理内存
    uint32_t phy_addr_start;//本内存池所管理物理内存的起始地址
    uint32_t pool_size;//字节容量
};

struct pool kernel_pool,user_pool;//内核内存池和用户内存池
struct virtual_addr kernel_vaddr;//用来给内核分配虚拟地址

//初始化
static void mem_pool_init(uint32_t all_mem){
    put_str("----------mem_pool_init----------\n");
    uint32_t page_table_size=PG_SIZE*256;

    // 页表大小 = 1 页的页目录表 + 第 0 和第 768 个页目录项指向同一个页表 +
// 第 769~1022 个页目录项共指向 254 个页表,共 256 个页框

    uint32_t used_mem=page_table_size+0x100000;

    uint32_t free_mem=all_mem-used_mem;
    uint16_t all_free_pages=free_mem/PG_SIZE;

    uint16_t kernel_free_pages=all_free_pages/2;
    uint16_t user_free_pages=all_free_pages-kernel_free_pages;

    uint32_t kbm_length=kernel_free_pages/8;//kernel bitmap长度

    uint32_t ubm_length=user_free_pages/8;//user bitmap长度

    uint32_t kp_start=used_mem;//kernel pool内核内存起始地址

    uint32_t up_start=kp_start+kernel_free_pages*PG_SIZE;//用户内存池的起始地址


    kernel_pool.phy_addr_start=kp_start;
    user_pool.phy_addr_start=up_start;

    kernel_pool.pool_size=kernel_free_pages*PG_SIZE;
    user_pool.phy_addr_start=user_free_pages*PG_SIZE;

    kernel_pool.pool_bitmap.btmp_bytes_len=kbm_length;
    user_pool.pool_bitmap.btmp_bytes_len=ubm_length;

    //-------------------------位图长度不固定，所以指定一块内存来生成位图-----------------------
    kernel_pool.pool_bitmap.bits=(void*)MEM_BITMAP_BASE;

    user_pool.pool_bitmap.bits=(void*)(MEM_BITMAP_BASE+kbm_length);


    //-------------------------输出内存池信息----------------------------------
    put_str("------------------kernel_pool_bitmap_start:");
    put_int((int)kernel_pool.pool_bitmap.bits);

    put_str("------------------kernel_pool_phy_addr_start:");
    put_int(kernel_pool.phy_addr_start);
    put_str("\n");

    put_str("------------------user_pool_bitmap_start:");
    put_str(user_pool.pool_bitmap.bits);

    put_str("-------------------user_pool_phy_addr_start:");
    put_str(user_pool.phy_addr_start);
    put_str("\n");

    //将位图置0
    bitmap_init(&kernel_pool.pool_bitmap);
    bitmap_init(&user_pool.pool_bitmap);    

    //初始化内核虚拟地址位图，按实际物理内存大小生成数组
    kernel_vaddr.vaddr_bitmap.btmp_bytes_len=kbm_length;

    kernel_vaddr.vaddr_bitmap.bits=(void*)(MEM_BITMAP_BASE+kbm_length+ubm_length);

    kernel_vaddr.vaddr_start=K_HEAP_START;
    bitmap_init(&kernel_vaddr.vaddr_bitmap);
    put_str("--------------------mem_pool_init done\n");

}

//内存管理部分入口

void mem_init(){
    put_str("----------------------mem_init_start--------------------------");
    uint32_t mem_bytes_total=(*(uint32_t*)(0xb00));
    mem_pool_init(mem_bytes_total);
    put_str("-----------------------mem_init_done--------------------------");
}