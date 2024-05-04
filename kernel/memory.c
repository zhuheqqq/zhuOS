#include "memory.h"
#include "stdint.h"
#include "print.h"
#include "bitmap.h"
#include "global.h"
#include "string.h"
#include "debug.h"
#include "sync.h"
#include "thread.h"
#include "interrupt.h"

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

//内存仓库
struct arena {
    struct mem_block_desc* desc;    //此arena关联的mem_block_desc
    uint32_t cnt;       //large为true时cnt表示的是页框数，否则cnt表示空闲mem_block数量
    bool large;
};

struct mem_block_desc k_block_descs[DESC_CNT];

struct pool{
    struct bitmap pool_bitmap;//本内存池用到的位图结构，用于管理物理内存
    uint32_t phy_addr_start;//本内存池所管理物理内存的起始地址
    uint32_t pool_size;//字节容量

    struct lock lock;//申请内存时互斥
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
        //用户内存池
        struct task_struct* cur = running_thread();//获取pcb指针
        bit_idx_start = bitmap_scan(&cur->userprog_vaddr.vaddr_bitmap, pg_cnt);//在位图中申请连续cnt个位
        if(bit_idx_start == -1) {
            return NULL;
        }

        while(cnt < pg_cnt) {
            //设置位图指定位
            bitmap_set(&cur->userprog_vaddr.vaddr_bitmap,bit_idx_start+cnt++,1);
        }

        vaddr_start=cur->userprog_vaddr.vaddr_start+bit_idx_start+PG_SIZE;

        ASSERT((uint32_t)vaddr_start<(0xc0000000-PG_SIZE));
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

void* get_user_pages(uint32_t pg_cnt) {
    lock_acquire(&user_pool.lock);
    void* vaddr=malloc_page(PF_USER,pg_cnt);
    memset(vaddr,0,pg_cnt*PG_SIZE);
    lock_release(&user_pool.lock);
    return vaddr;
}

//申请一页内存，并用vaddr映射到该页，可以指定虚拟地址
void* get_a_page(enum pool_flags pf, uint32_t vaddr) {
    struct pool* mem_pool=pf&PF_KERNEL?&kernel_pool:&user_pool;
    lock_acquire(&mem_pool->lock);

    //先将虚拟地址对应的位图置1
    struct task_struct* cur = running_thread();
    int32_t bit_idx = -1;

    //若当前是用户进程申请用户内存，就修改用户进程自己的虚拟地址位图
    if(cur->pgdir != NULL && pf == PF_USER) {
        bit_idx = (vaddr - cur->userprog_vaddr.vaddr_start)/PG_SIZE;
        ASSERT(bit_idx > 0);
        bitmap_set(&cur->userprog_vaddr.vaddr_bitmap, bit_idx, 1);

    }else if(cur->pgdir == NULL && pf == PF_KERNEL){
        //如果是内核线程申请内核内存就修改kernel_vaddr
        bit_idx = (vaddr - kernel_vaddr.vaddr_start) / PG_SIZE;
        ASSERT(bit_idx > 0);
        bitmap_set(&kernel_vaddr.vaddr_bitmap, bit_idx, 1);
    }else {
        PANIC("get_a_page:not allow kernel alloc userspace or user alloc kernelspace by get_a_page");
    }

    void* page_phyaddr = palloc(mem_pool);
    if(page_phyaddr == NULL) {
        return NULL;
    }

    page_table_add((void*)vaddr, page_phyaddr);

    lock_release(&mem_pool->lock);
    return (void*)vaddr;
}

//返回虚拟地址vaddr所映射到的物理地址
uint32_t addr_v2p(uint32_t vaddr) {
    uint32_t* pte = pte_ptr(vaddr);//得到虚拟地址vaddr所映射的物理页框起始地址

    return ((*pte & 0xfffff000) + (vaddr & 0x00000fff));
}

//安装1页大小的vaddr,专门针对fork时虚拟地址位图无需操作的情况
void* get_a_page_without_opvaddrbitmap(enum pool_flags pf, uint32_t vaddr) {
    struct pool* mem_pool = pf & PF_KERNEL ? &kernel_pool : &user_pool;
    lock_acquire(&mem_pool->lock);
    void* page_phyaddr = palloc(mem_pool);
    if(page_phyaddr == NULL) {
        lock_release(&mem_pool->lock);
        return NULL;
    }
    page_table_add((void*)vaddr, page_phyaddr);
    lock_release(&mem_pool->lock);
    return (void*)vaddr;
}

//初始化
static void mem_pool_init(uint32_t all_mem){
    put_str("----------------------------mem_pool_init start---------------------\n");
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
    put_str("------------------kernel_pool_bitmap_start:");
    put_int((int)kernel_pool.pool_bitmap.bits);
    put_str("\n");

    put_str("------------------kernel_pool_phy_addr_start:");
    put_int(kernel_pool.phy_addr_start);
    put_str("\n");

    put_str("------------------user_pool_bitmap_start:");
    put_int(user_pool.pool_bitmap.bits);
    put_str("\n");

    put_str("------------------user_pool_phy_addr_start:");
    put_int(user_pool.phy_addr_start);
    put_str("\n");

    //将位图置0
    bitmap_init(&kernel_pool.pool_bitmap);
    bitmap_init(&user_pool.pool_bitmap);    

    lock_init(&kernel_pool.lock);
    lock_init(&user_pool.lock);

    //初始化内核虚拟地址位图，按实际物理内存大小生成数组
    kernel_vaddr.vaddr_bitmap.btmp_bytes_len=kbm_length;

    kernel_vaddr.vaddr_bitmap.bits=(void*)(MEM_BITMAP_BASE+kbm_length+ubm_length);

    kernel_vaddr.vaddr_start=K_HEAP_START;
    bitmap_init(&kernel_vaddr.vaddr_bitmap);
    put_str("----------------------------mem_pool_init done---------------------\n");

}

//为malloc做准备  初始化内存块描述符
void block_desc_init(struct mem_block_desc* desc_array) {
    uint16_t desc_idx, block_size = 16;

    //初始化每个mem_block_desc描述符
    for(desc_idx = 0; desc_idx < DESC_CNT; desc_idx++) {
        desc_array[desc_idx].block_size = block_size;

        //初始化arena内存块数量
        desc_array[desc_idx].blocks_per_arena = \
            (PG_SIZE - sizeof(struct arena)) / block_size;

        list_init(&desc_array[desc_idx].free_list);

        block_size *=2; //更新为下一个规格内存块
    }
}

//返回arena中第idx个内存块的地址
static struct mem_block* arena2block(struct arena* a, uint32_t idx) {
    return (struct mem_block*) \
        ((uint32_t)a + sizeof(struct arena) + idx * a->desc->block_size);
}

//返回内存b所在的arena地址
static struct arena* block2arena(struct mem_block* b) {
    return (struct arena*)((uint32_t)b & 0xfffff000);
}

//在堆中申请size字节内存
void* sys_malloc(uint32_t size) {
    enum pool_flags PF;
    struct pool* mem_pool;
    uint32_t pool_size;
    struct mem_block_desc* descs;
    struct task_struct* cur_thread = running_thread();

    //判断用哪个内存池
    if(cur_thread->pgdir == NULL) {//内核线程
        PF = PF_KERNEL;
        pool_size = kernel_pool.pool_size;
        mem_pool = &kernel_pool;
        descs =k_block_descs;

    }else {
        //用户线程
        PF = PF_USER;
        pool_size = user_pool.pool_size;
        mem_pool = &user_pool;
        descs = cur_thread->u_block_desc;
    }

    //若申请的内存不在内存池容量范围内，则直接返回null
    if(!(size > 0 && size < pool_size)) {
        return NULL;
    }

    struct arena* a;    //指向新创建的arena
    struct mem_block* b;    //指向arena中的mem_block
    lock_acquire(&mem_pool->lock);

    //超过最大内存块1024,就分配页框
    if(size > 1024) {
        //大于1024计算需要的页框数
        uint32_t page_cnt = \
            DIV_ROUND_UP(size + sizeof(struct arena), PG_SIZE);

        //向上取整需要的页框数

        a = malloc_page(PF, page_cnt);

        if(a!=NULL) {
            memset(a, 0, page_cnt * PG_SIZE);   //将分配的内存清0

            //对于分配的大块页框，将desc置为null,cnt置为页框数,large置为true
            a->desc = NULL;
            a->cnt = page_cnt;
            a->large = true;
            lock_release(&mem_pool->lock);
            return (void*)(a + 1); //跨过arena
        }else{
            lock_release(&mem_pool->lock);
            return NULL;
        }
    }else {
        //申请内存小于等于1024,可在各个规格匹配合适的内存块规格
        uint8_t desc_idx;

        //从内存块描述符匹配合适的内存块规格
        for(desc_idx = 0; desc_idx < DESC_CNT; desc_idx++) {
            if(size <= descs[desc_idx].block_size) {
                break;
            }
        }

        //判断是否有可用的内存块
        if(list_empty(&descs[desc_idx].free_list)) {
            a = malloc_page(PF, 1);
            if(a == NULL) {
                lock_release(&mem_pool->lock);
                return NULL;
            }
            memset(a, 0, PG_SIZE);

            //小块内存，将desc置为相应内存块描述符，cnt置为arena可用的内存块数，large置为false
            a->desc = &descs[desc_idx];
            a->large = false;
            a->cnt = descs[desc_idx].blocks_per_arena;
            uint32_t block_idx;

            enum intr_status old_status = intr_disable();

            for(block_idx = 0; \ 
                block_idx < descs[desc_idx].blocks_per_arena;block_idx++) {
                    b = arena2block(a, block_idx);
                    ASSERT(!elem_find(&a->desc->free_list, &b->free_elem));
                    list_append(&a->desc->free_list, &b->free_elem);
                }
                intr_set_status(old_status);
        }

        //开始分配内存块
        b = elem2entry(struct mem_block, \
            free_elem,list_pop(&descs[desc_idx].free_list));
        
        memset(b, 0, descs[desc_idx].block_size);

        a = block2arena(b); //获取内存块b所在的arena
        a->cnt--;
        lock_release(&mem_pool->lock);
        return (void*)b;
    }
}

//将物理地址pg_phy_addr回收到物理内存池
void pfree(uint32_t pg_phy_addr) {
    struct pool* mem_pool;
    uint32_t bit_idx = 0;
    if(pg_phy_addr >= user_pool.phy_addr_start) {
        //用户物理内存池
        mem_pool = &user_pool;
        bit_idx = (pg_phy_addr - user_pool.phy_addr_start) / PG_SIZE;
    }else{
        mem_pool = &kernel_pool;
        bit_idx = (pg_phy_addr - kernel_pool.phy_addr_start) / PG_SIZE;
    }
    bitmap_set(&mem_pool->pool_bitmap, bit_idx, 0); //将位图中该位清0
}

//去掉页表中虚拟地址的映射，只去掉vaddr对应的pte
static void page_table_pte_remove(uint32_t vaddr) {
    uint32_t* pte = pte_ptr(vaddr);
    *pte &= ~PG_P_1;        //将页表项pte的P位置0
    asm volatile("invlpg %0"::"m"(vaddr):"memory");     //更新tlb
}

//在虚拟地址池中释放以vaddr起始的连续pg_cnt个虚拟页地址
static void vaddr_remove(enum pool_flags pf, \ 
    void* _vaddr, uint32_t pg_cnt) {
        uint32_t bit_idx_start = 0, vaddr = (uint32_t)_vaddr, cnt = 0;

        if(pf == PF_KERNEL) {
            //内核虚拟内存池
            bit_idx_start = (vaddr - kernel_vaddr.vaddr_start) / PG_SIZE;
            while(cnt < pg_cnt) {
                bitmap_set(&kernel_vaddr.vaddr_bitmap, \
                    bit_idx_start + cnt++, 0);
            }
        }else{
            //用户虚拟内存池
            struct task_struct* cur_thread = running_thread();
            bit_idx_start = \
                (vaddr - cur_thread->userprog_vaddr.vaddr_start) / PG_SIZE;
                while(cnt < pg_cnt) {
                    bitmap_set(&cur_thread->userprog_vaddr.vaddr_bitmap, \
                        bit_idx_start + cnt++, 0);
                }
        }
    }

//释放以虚拟内存地址_vaddr为起始的cnt个物理页框
/*
回收内存三步骤：
调用pfree清空物理地址位图中的相应位
再调用page_table_pte_remove删除页表中此地址的pte
最后调用vaddr_remove清楚虚拟地址位图中的相应位
*/
void mfree_page(enum pool_flags pf, void* _vaddr, uint32_t pg_cnt) {
    uint32_t pg_phy_addr;
    uint32_t vaddr = (int32_t)_vaddr, page_cnt = 0;
    ASSERT(pg_cnt >= 1 && vaddr % PG_SIZE == 0);
    pg_phy_addr = addr_v2p(vaddr);//获取对应的物理地址

    //确保待释放的物理内存存在，低端1mb+1kb大小页目录+1kb大小页表地址范围外
    ASSERT((pg_phy_addr % PG_SIZE) == 0 && pg_phy_addr >= 0x102000);

    //判断pg_phy_addr属于用户物理内存池还是内核物理内存池
    if(pg_phy_addr >= user_pool.phy_addr_start) {
        vaddr -= PG_SIZE;
        while(page_cnt < pg_cnt) {
            vaddr += PG_SIZE;
            pg_phy_addr = addr_v2p(vaddr);

            ASSERT((pg_phy_addr % PG_SIZE) == 0 && \
                pg_phy_addr >= user_pool.phy_addr_start);

            pfree(pg_phy_addr);

            page_table_pte_remove(vaddr);

            page_cnt++;
        }
        vaddr_remove(pf, _vaddr, pg_cnt);
    }else{
        vaddr -= PG_SIZE;
        while(page_cnt < pg_cnt) {
            vaddr += PG_SIZE;
            pg_phy_addr = addr_v2p(vaddr);

            ASSERT((pg_phy_addr % PG_SIZE) == 0 && \
                pg_phy_addr >= kernel_pool.phy_addr_start && \
                pg_phy_addr < user_pool.phy_addr_start);

            pfree(pg_phy_addr);

            page_table_pte_remove(vaddr);

            page_cnt++;
        }
        vaddr_remove(pf, _vaddr, pg_cnt);
    }
}

//回收内存
void sys_free(void* ptr) {
    ASSERT(ptr != NULL);
    if(ptr !=NULL) {
        enum pool_flags PF;
        struct pool* mem_pool;

        //判断是线程还是进程
        if(running_thread()->pgdir == NULL) {
            ASSERT((uint32_t)ptr >= K_HEAP_START);
            PF = PF_KERNEL;
            mem_pool = &kernel_pool;
        }else{
            PF = PF_USER;
            mem_pool = &user_pool;
        }

        lock_acquire(&mem_pool->lock);
        struct mem_block* b = ptr;
        struct arena* a = block2arena(b);//把mem_block转换成arena获取元信息

        ASSERT(a->large == 0 || a->large == 1);
        if(a->desc == NULL && a->large == true) {//>1024
            mfree_page(PF, a, a->cnt);
        }else{
            //<1024
            //先将内存回收到freelist
            list_append(&a->desc->free_list, &b->free_elem);

            if(++a->cnt == a->desc->blocks_per_arena) {
                uint32_t block_idx;
                for(block_idx = 0; block_idx < a->desc->blocks_per_arena; block_idx++) {
                    struct mem_block* b = arena2block(a, block_idx);
                    ASSERT(elem_find(&a->desc->free_list, &b->free_elem));
                    list_remove(&b->free_elem);
                }
                mfree_page(PF, a, 1);
            }
        }
        lock_release(&mem_pool->lock);

    }
}

//内存管理部分入口
void mem_init(){
    put_str("----------------------------mem_init_start--------------------------\n");
    uint32_t mem_bytes_total=(*(uint32_t*)(0xb00));
    mem_pool_init(mem_bytes_total);
    block_desc_init(k_block_descs);
    put_str("----------------------------mem_init_done---------------------------\n");
}