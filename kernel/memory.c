#include "memory.h"
#include "stdint.h"
#include "print.h"
#include "bitmap.h"
#include "global.h"
#include "string.h"
#include "debug.h"

#define PDE_IDX(addr)((addr&0xffc00000)>>22)  //返回虚拟地址高10位
#define PTE_IDX(addr)((addr&0x003ff000)>>12)    //返回虚拟地址中间10位


#define PG_SIZE 4096        //页的尺寸

/*----------------------------------------位图地址-----------------------------
0xc009f000是内核主线程栈顶，0xc009e00是内核主线程的pcb
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

//在pf表示的虚拟内存池中申请pg_cnt个虚拟页，成功则返回虚拟页的起始地址，失败返回NULL
static void* vaddr_get(enum pool_flags pf,uint32_t pg_cnt){
    int vaddr_start=0,bit_idx_start=-1;
    uint32_t cnt=0;
    if(pf==PF_KERNEL){
        bit_idx_start=bitmap_scan(&kernel_vaddr.vaddr_bitmap,pg_cnt);
        if(bit_idx_start==-1){
            return NULL;
        }
        while(cnt<pg_cnt){
            bitmap_set(&kernel_vaddr.vaddr_bitmap,bit_idx_start+cnt++,1);
        }
        vaddr_start=kernel_vaddr.vaddr_start+bit_idx_start*PG_SIZE;
    }else{
        //用户内存池，将来实现用户进程再补充
    }
    return (void*)vaddr_start;
}

//得到虚拟地址vaddr对应的pte指针
uint32_t* pte_ptr(uint32_t vaddr){
    //页表自己+用页目录项pde（页目录项页表内的索引）作为pte的索引访问到页表+pte索引作为页内偏移
    uint32_t* pte=(uint32_t*)(0xffc00000+((vaddr&0xffc00000)>>10)+PTE_IDX(vaddr)*4);
    return pte;
}

//得到虚拟地址vaddr对应的pde指针
uint32_t* pde_ptr(uint32_t vaddr){
    uint32_t* pde=(uint32_t*)((0xfffff000)+PDE_IDX(vaddr)*4);
    return pde;
}

//在m_pool指向的物理内存池中分配1个物理页，成功返回页框的物理地址，失败返回NULL
static void* palloc(struct pool* m_pool){
    //扫描或设置位图要保证原子操作
    int bit_idx=bitmap_scan(&m_pool->pool_bitmap,1);
    if(bit_idx==-1){
        return NULL;
    }
    bitmap_set(&m_pool->pool_bitmap,bit_idx,1);
    uint32_t page_phyaddr=((bit_idx*PG_SIZE)+m_pool->phy_addr_start);
    return (void*)page_phyaddr;
}

//页表中添加虚拟地址_vaddr与物理地址_page_phyaddr的映射
static void page_table_add(void* _vaddr,void* _page_phyaddr){
    uint32_t vaddr=(uint32_t)_vaddr,page_phyaddr=(uint32_t)_page_phyaddr;
    uint32_t* pde=pde_ptr(vaddr);
    uint32_t* pte=pte_ptr(vaddr);

    /*执行*pte,会访问到空的pde,所以确保pde创建完成后才能执行*pte,
    否则会引发page_fault.在*pde为0时，*pte只能出现在下面else语句的*pde后面
    */
   //判断页目录项的p位看是否存在
   if(*pde&0x00000001){
        ASSERT(!(*pte&0x00000001));
        if(!(*pte&0x00000001)){
            *pte=(page_phyaddr|PG_US_U|PG_RW_W|PG_P_1);
        }else{
            PANIC("pte repeat");
            *pte=(page_phyaddr|PG_US_U|PG_RW_W|PG_P_1);
        }

   }else{
        //页目录不存在，所以要先创建目录再创建页表项
        uint32_t pde_phyaddr=(uint32_t)palloc(&kernel_pool);

        *pde=(pde_phyaddr|PG_US_U|PG_RW_W|PG_P_1);

        /*分配到的物理页地址pde_phyaddr对应的物理内存清0，避免里面的旧数据变成了页表项*/
        memset((void*)((int)pte&0xfffff000),0,PG_SIZE);

        ASSERT(!(*pte&0x00000001));
        *pte=(page_phyaddr|PG_US_U|PG_RW_W|PG_P_1);
   }

}

//分配pg_cnt个页空间，成功则返回起始虚拟地址，失败时返回NULL
void* malloc_page(enum pool_flags pf,uint32_t pg_cnt){
    ASSERT(pg_cnt>0&&pg_cnt<3840);

    /***********
 malloc_page 的原理是三个动作的合成:
 ***********
 1 通过 vaddr_get 在虚拟内存池中申请虚拟地址
 2 通过 palloc 在物理内存池中申请物理页
 3 通过 page_table_add 将以上得到的虚拟地址和物理地址在页表中完成映射
********************************************************************/
    void* vaddr_start=vaddr_get(pf,pg_cnt);
    if(vaddr_start==NULL){
        return NULL;
    }

    uint32_t vaddr=(uint32_t)vaddr_start,cnt=pg_cnt;
    struct pool* mem_pool=pf&PF_KERNEL?&kernel_pool:&user_pool;

    //因为是虚拟地址是连续的，但物理地址可以是不连续的，所以逐个做映射
    while(cnt-->0){
        void* page_phyaddr=palloc(mem_pool);
        if(page_phyaddr==NULL){
            //失败要将已申请的内存全部返回
            return NULL;
        }
        page_table_add((void*)vaddr,page_phyaddr);//在页表中做映射
        vaddr+=PG_SIZE;
    }

    return vaddr_start;
}

//从内核物理池中申请1页内存
void* get_kernel_pages(uint32_t pg_cnt){
    void* vaddr=malloc_page(PF_KERNEL,pg_cnt);
    if(vaddr!=NULL){
        memset(vaddr,0,pg_cnt*PG_SIZE);
    }
    return vaddr;
}

//初始化
static void mem_pool_init(uint32_t all_mem){
    put_str("------------------------mem_pool_init start--------------------------------\n");
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
    user_pool.pool_size=user_free_pages*PG_SIZE;

    kernel_pool.pool_bitmap.btmp_bytes_len=kbm_length;
    user_pool.pool_bitmap.btmp_bytes_len=ubm_length;

    //-------------------------位图长度不固定，所以指定一块内存来生成位图-----------------------
    kernel_pool.pool_bitmap.bits=(void*)MEM_BITMAP_BASE;

    user_pool.pool_bitmap.bits=(void*)(MEM_BITMAP_BASE+kbm_length);


    //-------------------------输出内存池信息----------------------------------
    put_str("------------------kernel_pool_bitmap_start:]");
    put_int((int)kernel_pool.pool_bitmap.bits);
    put_str("\n");

    put_str("------------------kernel_pool_phy_addr_start:");
    put_int(kernel_pool.phy_addr_start);
    put_str("\n");

    put_str("------------------user_pool_bitmap_start:");
    put_int(user_pool.pool_bitmap.bits);
    put_str("\n");

    put_str("-------------------user_pool_phy_addr_start:");
    put_int(user_pool.phy_addr_start);
    put_str("\n");

    //将位图置0
    bitmap_init(&kernel_pool.pool_bitmap);
    bitmap_init(&user_pool.pool_bitmap);    

    //初始化内核虚拟地址位图，按实际物理内存大小生成数组
    kernel_vaddr.vaddr_bitmap.btmp_bytes_len=kbm_length;

    kernel_vaddr.vaddr_bitmap.bits=(void*)(MEM_BITMAP_BASE+kbm_length+ubm_length);

    kernel_vaddr.vaddr_start=K_HEAP_START;
    bitmap_init(&kernel_vaddr.vaddr_bitmap);
    put_str("--------------------mem_pool_init done------------------------\n");

}

//内存管理部分入口

void mem_init(){
    put_str("----------------------mem_init_start--------------------------\n");
    uint32_t mem_bytes_total=(*(uint32_t*)(0xb00));
    mem_pool_init(mem_bytes_total);
    put_str("-----------------------mem_init_done--------------------------\n");
}