#ifndef __FS_FILE_H
#define __FS_FILE_H
#include "stdint.h"
#include "ide.h"
#include "dir.h"
#include "global.h"

//文件结构
struct file {
    uint32_t fd_pos;//记录当前文件操作的偏移地址，以0为起始地址，最大为文件大小
    uint32_t fd_flag;
    struct inode* fd_inode;//用来指向inode队列(part->open_inodes)中的inode
};

//标准输入输出描述符
enum std_fd {
    stdin_no,//0
    stdout_no,//1
    stderr_no //2
};

//位图类型
enum bitmap_type{
    INODE_BITMAP,//inode位图
    BLOCK_BITMAP//块位图
};


#define MAX_FILE_OPEN 32 //系统可打开的最大文件数

#endif