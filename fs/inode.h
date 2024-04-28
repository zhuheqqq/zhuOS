#ifndef __FS_INODE_H
#define __FS_INODE_H
#include "stdint.h"
#include "list.h"

//inode结构
struct inode{
    uint32_t i_no;//inode编号

    //if the inode is fs_inode, then i_size is the size of the file
    //否则，是该目录下所有目录项之和

    uint32_t i_size;

    uint32_t i_open_cnts;//记录此文件被打开的次数
    bool write_deny;//写文件不能并行，进程写文件前检查此标识

    uint32_t i_sectors[13];//0-12是直接块，12是存储一级间接块指针
    struct list_elem inode_tag;
};

#endif