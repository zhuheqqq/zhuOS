#include "fs.h"
#include "super_block.h"
#include "inode.h"
#include "dir.h"
#include "stdint.h"
#include "stdio-kernel.h"
#include "list.h"
#include "string.h"
#include "ide.h"
#include "global.h"
#include "debug.h"
#include "memory.h"
#include "file.h"

struct partition* cur_part; //默认情况下操作的是哪个分区

//在分区链表中找到名为part_name的分区，并将其指针赋值给cur_part
//是list_traversal的回调函数
static bool mount_partition(struct list_elem* pelem, int arg) {
    char* part_name = (char*)arg;
    struct partition* part = elem2entry(struct partition, part_tag, pelem);
    if(!strcmp(part->name, part_name)) {
        cur_part = part;
        struct disk* hd = cur_part->my_disk;

        //sb_buf存储从硬盘读入的超级块
        struct super_block* sb_buf = (struct super_block*)sys_malloc(SECTOR_SIZE);

        //在内存中创建分区cur_part的超级块
        cur_part->sb = (struct super_block*)sys_malloc(sizeof(struct super_block));
        if(cur_part->sb == NULL) {
            PANIC("alloc memory failed!");
        }

        //读入超级块
        memset(sb_buf, 0, SECTOR_SIZE);
        ide_read(hd, cur_part->start_lba + 1, sb_buf, 1);

        //把sb_buf中的超级块的信息复制到分区的超级块sb中
        memcpy(cur_part->sb, sb_buf, sizeof(struct super_block));

        //将硬盘上的块位图读入到内存
        cur_part->block_bitmap.bits = (uint8_t*)sys_malloc(sb_buf->block_bitmap_sects * SECTOR_SIZE);

        if(cur_part->block_bitmap.bits == NULL) {
            PANIC("alloc memory failed!");
        }
        cur_part->block_bitmap.btmp_bytes_len = sb_buf->block_bitmap_sects * SECTOR_SIZE;

        //将硬盘上的inode位图读入到内存
        cur_part->inode_bitmap.bits = (uint8_t*)sys_malloc(sb_buf->inode_bitmap_sects * SECTOR_SIZE);
        if (cur_part->inode_bitmap.bits == NULL) {
            PANIC("alloc memory failed!");
        }
        cur_part->inode_bitmap.btmp_bytes_len = sb_buf->inode_bitmap_sects * SECTOR_SIZE;
        //从硬盘上读入块位图到分区的block_bitmap_bits
        ide_read(hd, sb_buf->block_bitmap_lba, cur_part->inode_bitmap.bits, sb_buf->inode_bitmap_sects);

        list_init(&cur_part->open_inodes);
        printk("mount %s done!\n",part->name);


        return true;//只有返回true  list_traversal才会停止遍历
    }
    return false;
}

/*
根据分区part大小，计算分区文件系统各元信息需要的扇区数及位置
在内存中创建超级块，将上述计算步骤的元信息写入超级块
将超级块写入磁盘
将元信息写入磁盘
将根目录写入磁盘
*/
// 格式化分区，初始化分区的元信息，创建文件系统
static void partition_format(struct partition *part)
{
    uint32_t boot_sector_sects = 1;
    uint32_t super_block_sects = 1;
    uint32_t inode_bitmap_sects = DIV_ROUND_UP(MAX_FILES_PER_PART, BITS_PER_SECTOR);

    uint32_t inode_table_sects = DIV_ROUND_UP(((sizeof(struct inode) * MAX_FILES_PER_PART)), SECTOR_SIZE);
    uint32_t used_sects = boot_sector_sects + super_block_sects + inode_bitmap_sects + inode_table_sects;
    uint32_t free_sects = part->sec_cnt - used_sects;

    // 简单处理块位图占据的扇区数
    uint32_t block_bitmap_sects;
    block_bitmap_sects = DIV_ROUND_UP(free_sects, BITS_PER_SECTOR);
    /* block_bitmap_bit_len 是位图中位的长度,也是可用块的数量 */
    uint32_t block_bitmap_bit_len = free_sects - block_bitmap_sects;
    block_bitmap_sects = DIV_ROUND_UP(block_bitmap_bit_len, BITS_PER_SECTOR);

    // 超级块初始化
    struct super_block sb;
    sb.magic = 0x19590318;
    sb.sec_cnt = part->sec_cnt;
    sb.inode_cnt = MAX_FILES_PER_PART;
    sb.part_lba_base = part->start_lba;

    sb.block_bitmap_lba = sb.part_lba_base + 2;
    // 第0块是引导块，1块是超级块
    sb.block_bitmap_sects = block_bitmap_sects;

    sb.inode_bitmap_lba = sb.block_bitmap_lba + sb.inode_bitmap_sects;
    sb.inode_bitmap_sects = inode_bitmap_sects;

    sb.inode_table_lba = sb.inode_bitmap_lba + sb.inode_table_sects;
    sb.inode_table_sects = inode_table_sects;

    sb.data_start_lba = sb.inode_table_lba + sb.inode_table_sects;
    sb.root_inode_no = 0;
    sb.dir_entry_size = sizeof(struct dir_entry);

    printk("%s info:\n", part->name);
    printk("   magic:0x%x\n   part_lba_base:0x%x\n   all_sectors:0x%x\n   inode_cnt:0x%x\n   block_bitmap_lba:0x%x\n   block_bitmap_sectors:0x%x\n   inode_bitmap_lba:0x%x\n   inode_bitmap_sectors:0x%x\n   inode_table_lba:0x%x\n   inode_table_sectors:0x%x\n   data_start_lba:0x%x\n", sb.magic, sb.part_lba_base, sb.sec_cnt, sb.inode_cnt, sb.block_bitmap_lba, sb.block_bitmap_sects, sb.inode_bitmap_lba, sb.inode_bitmap_sects, sb.inode_table_lba, sb.inode_table_sects, sb.data_start_lba);


    
    struct disk* hd = part->my_disk;
    //1.将超级块写入本分区的1扇区
    ide_write(hd, part->start_lba + 1, &sb, 1);
    printk("    super_block_lba:0x%x\n", part->start_lba + 1);

    //找到最大的元信息，用其尺寸做储存缓冲区
    uint32_t buf_size = (sb.block_bitmap_sects >= sb.inode_bitmap_sects ? sb.block_bitmap_sects : sb.inode_bitmap_sects);

    buf_size = (buf_size >= sb.inode_table_sects ? buf_size : sb.inode_table_sects) * SECTOR_SIZE;

    uint8_t* buf = (uint8_t*)sys_malloc(buf_size);//申请内存有内存管理系统清0返回

    //2.将块图初始化并写入sb.block_bitmap_lba

    //初始化块位图block_bitmap
    buf[0] |= 0x01; //第0个块留着根目录，占位
    uint32_t block_bitmap_last_byte = block_bitmap_bit_len / 8;
    uint8_t block_bitmap_last_bit = block_bitmap_bit_len % 8;
    uint32_t last_size = SECTOR_SIZE - (block_bitmap_last_byte % SECTOR_SIZE);//last_size是位图所在最后一个扇区

    //1.将位图最后一字节置1,超出实际块数的部分置为已占用
    memset(&buf[block_bitmap_last_byte], 0xff, last_size);

    //2.将上一步覆盖的最后一字节内的有效位重新置0
    uint8_t bit_idx = 0;
    while(bit_idx <= block_bitmap_last_bit) {
        buf[block_bitmap_last_byte] &= ~(1 << bit_idx++);
    }
    ide_write(hd, sb.block_bitmap_lba, buf, sb.block_bitmap_sects);

    //3.将inode位图初始化并写入sb.inode_bitmap_lba
    //先清空缓冲区
    memset(buf, 0, buf_size);
    buf[0] |= 0x1;//第0个inode分给了根目录

    ide_write(hd, sb.inode_bitmap_lba, buf, sb.inode_bitmap_sects);

    //4.将inode数组初始化并写入sb.inode_table_lba
    //写根目录所在的inode
    memset(buf, 0, buf_size);
    struct inode* i =(struct inode*)buf;
    i->i_size = sb.dir_entry_size * 2;
    i->i_no = 0;
    i->i_sectors[0] = sb.data_start_lba;

    ide_write(hd, sb.inode_table_lba, buf, sb.inode_table_sects);

    //5.将根目录写入sb.data_start_lba

    //写入.和..两个目录项
    memset(buf, 0, buf_size);
    struct dir_entry* p_de = (struct dir_entry*)buf;

    //初始化当前目录
    memcpy(p_de->filename, ".", 1);
    p_de->i_no = 0;
    p_de->f_type = FT_DIRECTORY;
    p_de++;

    //初始化当前父目录..
    memcpy(p_de->filename, "..", 2);
    p_de->i_no = 0;
    p_de->f_type = FT_DIRECTORY;

    ide_write(hd, sb.data_start_lba, buf, 1);

    printk("    root_dir_lba:0x%x\n", sb.data_start_lba);
    printk("%s format done\n", part->name);
    sys_free(buf); 

}

//解析命令
static char* path_parse(char* pathname, char* name_store) {
    if(pathname[0] == '/') {//根目录不用单独解析
        while(*(++pathname) == '/');//跳过////
    }
    //开始一般路径解析
    while(*pathname != '/' && *pathname != 0) {
        *name_store++ = *pathname++;
    }
    if(pathname[0] == 0) {
        //如果路径为空，返回NULL
        return NULL;
    }
    return pathname;

}

//返回路径深度
int32_t path_depth_cnt(char* pathname) {
    ASSERT(pathname != NULL);
    char* p = pathname;
    char name[MAX_FILE_NAME_LEN];

    uint32_t depth = 0;

    //解析路径
    p = path_parse(p, name);
    while(name[0]) {
        depth++;
        memset(name, 0, MAX_FILE_NAME_LEN);
        if(p) {
            p = path_parse(p, name);
        }
    }
    return depth;
}

//搜索文件，找到返回inode号
static int search_file(const char* pathname, struct path_search_record* searched_record) {
    //如果是根目录直接返回
    if(!strcmp(pathname, "/") || !strcmp(pathname, "/.") || !strcmp(pathname, "/..")) {
        searched_record->parent_dir = &root_dir;
        searched_record->file_type = FT_DIRECTORY;
        searched_record->searched_path[0] = 0;  //搜索路径置空
        return 0;
    }

    uint32_t path_len = strlen(pathname);

    ASSERT(pathname[0] == '/' && path_len > 1 && path_len < MAX_PATH_LEN);
    char* sub_path = (char*)pathname;
    struct dir* parent_dir = &root_dir;
    struct dir_entry dir_e;

    //记录路径解析出来的各级名称
    char name[MAX_FILE_NAME_LEN] = {0};

    searched_record->parent_dir = parent_dir;
    searched_record->file_type = FT_UNKNOWN;
    uint32_t parent_inode_no = 0;//父目录的inode号

    sub_path = path_parse(sub_path, name);
    while(name[0]){
        //结束符结束循环
        ASSERT(strlen(searched_record->searched_path) < 512);

        //记录已存在的父目录
        strcat(searched_record->searched_path, "/");
        strcat(searched_record->searched_path, name);

        //在所给目录中查找文件
        if(search_dir_entry(cur_part, parent_dir, name, &dir_e)) {
            memset(name, 0, MAX_FILE_NAME_LEN);
            if(sub_path) {
                sub_path = path_parse(sub_path, name);
            }

            if(FT_DIRECTORY == dir_e.f_type) {//打开的是目录
                parent_inode_no = parent_dir->inode->i_no;
                dir_close(parent_dir);
                parent_dir = dir_open(cur_part, dir_e.i_no);
                searched_record->parent_dir = parent_dir;
                continue;
            }else if(FT_REGULAR == dir_e.f_type) {
                //普通文件
                searched_record->file_type = FT_REGULAR;
                return dir_e.i_no;
            }
        }else{
            return -1;
        }
    }
    dir_close(searched_record->parent_dir);

    //保存被查找的直接父目录
    searched_record->parent_dir = dir_open(cur_part, parent_inode_no);
    searched_record->file_type = FT_DIRECTORY;
    return dir_e.i_no;
}

//打开或创建文件成功后，返回文件描述符
int32_t sys_open(const char* pathname, uint8_t flags) {
    //open文件
    if(pathname[strlen(pathname) - 1] == '/') {
        printk("can't open a directory %s\n", pathname);
        return -1;
    }
    ASSERT(flags <= 7);
    int32_t fd = -1;

    struct path_search_record searched_record;
    memset(&searched_record, 0, sizeof(struct path_search_record));

    //记录目录深度
    uint32_t pathname_depth = path_depth_cnt((char*)pathname);

    //先检查文件是否存在
    int inode_no = search_file(pathname, &searched_record);
    bool found = inode_no != -1 ? true : false;

    if(searched_record.file_type == FT_DIRECTORY) {
        printk("can't open a directory with open(), use opendir() to instead\n");
        dir_close(searched_record.parent_dir);
        return -1;
    }

    uint32_t path_searched_depth = path_depth_cnt(searched_record.searched_path);

    //判断是否将pathname各层目录都访问到了
    if(pathname_depth != path_searched_depth) {
        printk("can't access %s: Not a directory, subpath %s is't exist\n",pathname, searched_record.parent_dir);
        return -1;
    }

    //若是在最后一个路径上没找到,并且并不是要创建文件,直接返回-1
    if(!found && !(flags & O_CREAT)) {
        printk("in path %s, file %s is't exist\n",searched_record.searched_path,(strrchr(searched_record.searched_path, '/') + 1));
        dir_close(searched_record.parent_dir);
        return -1;
    }else if(found && flags & O_CREAT) {
        //要创建的文件已存在
        printk("%s has already exist!\n",pathname);
        dir_close(searched_record.parent_dir);
        return -1;
    }

    switch(flags & O_CREAT) {
        case O_CREAT:
            printk("creating file...\n");
            fd = file_create(searched_record.parent_dir, (strrchr(pathname, '/') + 1), flags);
            dir_close(searched_record.parent_dir);
            //其余为打开文件
        default:
            fd = file_open(inode_no, flags);
    }

    return fd;//pcb->fd_table数组的元素下标

}

//将文件描述符转化为文件表的下标
static uint32_t fd_local2global(uint32_t local_fd) {
    struct task_struct* cur = running_thread();
    int32_t global_fd = cur->fd_table[local_fd];
    ASSERT(global_fd >=0 && global_fd < MAX_FILE_OPEN);
    return (uint32_t)global_fd;
}

//关闭文件描述符fd指向的文件，成功返回0,否则返回-1
int32_t sys_close(int32_t fd) {
    int32_t ret = -1;
    if(fd > 2) {
        uint32_t _fd = fd_local2global(fd);
        ret = file_close(&file_table[_fd]);
        running_thread()->fd_table[fd] = -1;//使该文件描述符可用
    }
    return ret;
}

//在磁盘搜索文件系统,若没有则格式化分区创建文件系统
void filesys_init() {
    uint8_t channel_no = 0, dev_no, part_idx = 0;

    //sb_buf用来存储从硬盘上读入的超级块
    struct super_block* sb_buf = (struct super_block*)sys_malloc(SECTOR_SIZE);

    if(sb_buf == NULL) {
        PANIC("alloc memory failed!");
    }
    printk("searching filesystem .........\n");
    while(channel_no < channel_cnt) {
        dev_no = 0;
        while(dev_no < 2) {
            if(dev_no == 0){
                //跨过hd60M.img
                dev_no++;
                continue;

            }
            struct disk* hd = &channels[channel_no].devices[dev_no];
            struct partition* part = hd->prim_parts;
            while(part_idx < 12) {//4个主分区和8个逻辑分区
                if(part_idx == 4){
                    part = hd->logic_parts;
                }

                //channels数组是全局变量，默认值为0,disk是嵌套结构，partition是disk的嵌套结构，所以partition的成员默认为0

                //处理存在的分区
                if(part->sec_cnt != 0){
                    //如果分区存在
                    memset(sb_buf, 0, SECTOR_SIZE);

                    //读出超级块，根据魔数判断是否存在于文件系统
                    ide_read(hd, part->start_lba + 1, sb_buf, 1);

                    //只支持自己的文件系统，若磁盘上已经有文件系统就不再格式化了
                    if(sb_buf->magic == 0x19590318) {
                        printk("%s has filesystem\n", part->name);
                    }else{//没有发现魔数为..的文件系统，开始创建
                        //其他文件系统不支持，一律按无文件系统处理
                        printk("formatting %s's partition %s........\n",hd->name,part->name);
                        partition_format(part);
                    }
                }
                part_idx++;
                part++;//下一分区

            }
            dev_no++;//下一磁盘
        }
        channel_no++;//下一通道
    }
    sys_free(sb_buf);

    char default_part[8] = "sdb1";//确定操作的默认分区
    //挂载分区
    list_traversal(&partition_list, mount_partition, (int)default_part);

    //将当前分区根目录打开
    open_root_dir(cur_part);

    //初始化文件表
    uint32_t fd_idx = 0;
    while(fd_idx < MAX_FILE_OPEN){
        file_table[fd_idx++].fd_inode = NULL;
    }

    printk("filesys_init end...\n");
}



