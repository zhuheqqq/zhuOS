#include "inode.h"
#include "fs.h"
//#include "file.h"
#include "global.h"
#include "debug.h"
#include "memory.h"
#include "interrupt.h"
#include "list.h"
#include "stdio-kernel.h"
#include "string.h"
#include "super_block.h"

//用来存储inode位置
struct inode_position {
    bool two_sec;   //inode是否跨扇区
    uint32_t sec_lba;//inode所在扇区号
    uint32_t off_size;//inode在扇区内的字节偏移量
};

//获取inode所在扇区和扇区内的偏移量
static void inode_locate(struct partition* part, uint32_t inode_no, struct inode_position* inode_pos) {

    //inode_table在硬盘上是连续的
    ASSERT(inode_no < 4096);
    uint32_t inode_table_lba = part->sb->inode_table_lba;

    uint32_t inode_size = sizeof(struct inode);
    uint32_t off_size = inode_no*inode_size;// 第 inode_no 号 I 结点相对于 inode_table_lba 的字节偏移量
    uint32_t off_sec = off_size / 512;// 第 inode_no 号 I 结点相对于 inode_table_lba 的扇区偏移量

    uint32_t off_size_in_sec = off_size % 512;//待查找的inode所在扇区的起始地址

    //判断此i结点是否跨越2个扇区
    uint32_t left_in_sec = 512 - off_size_in_sec;
    if(left_in_sec < inode_size) {//如果扇区剩下的空间不足以容纳一个inode,肯定跨越了2个扇区
        inode_pos->two_sec = true;
    }else{
        inode_pos->two_sec = false;
    }

    inode_pos->sec_lba = inode_table_lba + off_sec;
    inode_pos->off_size = off_size_in_sec;
}

//将inode写入分区part
void inode_sync(struct partition* part, struct inode* inode, void* io_buf) {
    //io_buf是用于硬盘io的缓冲区
    uint8_t inode_no = inode->i_no;
    struct inode_position inode_pos;

    inode_locate(part, inode_no, &inode_pos);//inode信息存入inode_pos

    ASSERT(inode_pos.sec_lba <= (part->start_lba + part->sec_cnt));

    struct inode pure_inode;
    memcpy(&pure_inode, inode, sizeof(struct inode));

    pure_inode.i_open_cnts = 0;
    pure_inode.write_deny = false;//在硬盘中读出时可写
    pure_inode.inode_tag.prev = pure_inode.inode_tag.next = NULL;

    char* inode_buf = (char*)io_buf;
    if(inode_pos.two_sec) {
        ide_read(part->my_disk, inode_pos.sec_lba, inode_buf, 2);//inode写入硬盘时是连续写入的，所以读入2块扇区

        //开始将待写入的inode拼入到这2个扇区中的响应位置
        memcpy((inode_buf + inode_pos.off_size), &pure_inode, sizeof(struct inode));

        //将拼接好的数据再写入磁盘
        ide_write(part->my_disk, inode_pos.sec_lba, inode_buf, 2);
    }else{
        ide_read(part->my_disk, inode_pos.sec_lba, inode_buf, 1);
        memcpy((inode_buf + inode_pos.off_size), &pure_inode, sizeof(struct inode));
        ide_write(part->my_disk, inode_pos.sec_lba, inode_buf, 1);
    }
}