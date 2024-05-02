#include "file.h"
#include "fs.h"
#include "super_block.h"
#include "inode.h"
#include "stdio-kernel.h"
#include "memory.h"
#include "debug.h"
#include "interrupt.h"
#include "string.h"
#include "thread.h"
#include "global.h"

#define DEFAULT_SECS 1

struct file file_table[MAX_FILE_OPEN];

//从文件表file_table中获取一个空闲位，成功返回下标，失败返回-1
int32_t get_free_slot_in_global(void) {
    uint32_t fd_idx = 3;
    while(fd_idx < MAX_FILE_OPEN) {
        if(file_table[fd_idx].fd_inode == NULL) {
            break;
        }
        fd_idx++;
    }
    if(fd_idx == MAX_FILE_OPEN) {
        printk("exceed max open files\n");
        return -1;
    }
    return fd_idx;
}

//将全局描述符下标安装到进程或线程自己的文件描述符数组fd_table中
int32_t pcb_fd_install(int32_t global_fd_idx) {
    struct task_struct* cur = running_thread();
    uint8_t local_fd_idx = 3;//跨过stdin,stdout,stderr
    while(local_fd_idx < MAX_FILES_OPEN_PER_PROC) {
        if(cur->fd_table[local_fd_idx] == -1) {
            cur->fd_table[local_fd_idx] = global_fd_idx;
            break;
        }
        local_fd_idx++;
    }
    if(local_fd_idx == MAX_FILES_OPEN_PER_PROC) {
        printk("exceed max open files_per_proc\n");
        return -1;
    }
    return local_fd_idx;
}

//分配一个i结点，返回i结点号
int32_t inode_bitmap_alloc(struct partition* part) {
    int32_t bit_idx = bitmap_scan(&part->inode_bitmap, 1);
    if(bit_idx == -1) {
        return -1;
    }
    bitmap_set(&part->inode_bitmap, bit_idx, 1);
    return bit_idx;
}

//分配1个扇区，返回其扇区地址
int32_t block_bitmap_alloc(struct partition* part) {
    int32_t bit_idx = bitmap_scan(&part->block_bitmap, 1);
    if(bit_idx == -1){
        return -1;
    }
    bitmap_set(&part->block_bitmap, bit_idx, 1);//和inode_bitmap_malloc不同，此处返回的不是位图索引,而是具体可用的扇区地址
    return (part->sb->data_start_lba + bit_idx);
}

//将内存中bitmap第bit_idx位所在512字节同步到硬盘
void bitmap_sync(struct partition* part, uint32_t bit_idx, uint8_t btmp) {
    uint32_t off_sec = bit_idx / 4096;//本i结点索引相对于位图的扇区偏移量
    uint32_t off_size = off_sec * BLOCK_SIZE;//本i结点索引相对于位图的字节偏移量

    uint32_t sec_lba;
    uint8_t* bitmap_off;

    //需要被同步到硬盘的位图只有inode_bitmap和block_bitmap
    switch (btmp) {
        case INODE_BITMAP:
            sec_lba = part->sb->inode_bitmap_lba + off_sec;
            bitmap_off = part->inode_bitmap.bits + off_size;
            break;

        case BLOCK_BITMAP:
            sec_lba = part->sb->block_bitmap_lba + off_sec;
            bitmap_off = part->block_bitmap.bits + off_size;
            break;
    }
    ide_write(part->my_disk, sec_lba, bitmap_off, 1);
}

//创建文件，成功返回文件描述符
int32_t file_create(struct dir* parent_dir, char* filename, uint8_t flag) {
    //公共缓冲区
    void* io_buf = sys_malloc(1024);
    if(io_buf == NULL) {
        printk("in file_creat:sys_malloc for io_buf failed\n");
        return -1;
    }

    uint8_t rollback_step = 0;//操作失败时回滚各资源状态

    //为新文件分配inode
    int32_t inode_no = inode_bitmap_alloc(cur_part);
    if(inode_no == -1) {
        printk("in file_creat:allocate inode failed\n");
        return -1;
    }

    struct inode* new_file_inode = (struct inode*)sys_malloc(sizeof(struct inode));
    if(new_file_inode == NULL) {
        printk("file_creat:sys_malloc for inode failed\n");
        rollback_step = 1;
        goto rollback;
    }
    inode_init(inode_no, new_file_inode);//初始化i结点

    //返回数组下标
    int fd_idx = get_free_slot_in_global();
    if(fd_idx == -1) {
        printk("exceed max open files\n");
        rollback_step = 2;
        goto rollback;
    }

    file_table[fd_idx].fd_inode = new_file_inode;
    file_table[fd_idx].fd_pos = 0;
    file_table[fd_idx].fd_flag = flag;
    file_table[fd_idx].fd_inode->write_deny = false;

    struct dir_entry new_dir_entry;
    memset(&new_dir_entry, 0, sizeof(struct dir_entry));

    create_dir_entry(filename, inode_no, FT_REGULAR, &new_dir_entry);

    //同步数据
    //1. 在目录项下安装new_dir_entry目录项
    if(!sync_dir_entry(parent_dir, &new_dir_entry, io_buf)) {
        printk("sync dir_entry to disk failed\n");
        rollback_step = 3;
        goto rollback;
    }

    memset(io_buf, 0, 1024);

    //2.将父目录i结点内容同步到硬盘
    inode_sync(cur_part, parent_dir->inode, io_buf);
    memset(io_buf, 0, 1024);

    //3.将新创建的文件i结点内容同步到硬盘
    inode_sync(cur_part, new_file_inode, io_buf);

    //4.将inode_bitmap位图同步到硬盘
    bitmap_sync(cur_part, inode_no, INODE_BITMAP);

    //5.将创建的文件i结点添加到open_inodes链表
    list_push(&cur_part->open_inodes, &new_file_inode->inode_tag);
    new_file_inode->i_open_cnts = 1;

    sys_free(io_buf);
    return pcb_fd_install(fd_idx);

    //某部失败则回滚
rollback:
    switch(rollback_step) {
        case 3:
            memset(&file_table[fd_idx], 0, sizeof(struct file));
        case 2:
            sys_free(new_file_inode);
        case 1:
        bitmap_set(&cur_part->inode_bitmap, inode_no, 0);
        break;
    }
    sys_free(io_buf);
    return -1;
}

//打开编号为inode_no的inode对应的文件，成功返回文件描述符，失败返回-1
int32_t file_open(uint32_t inode_no, uint8_t flag) {
    int fd_idx = get_free_slot_in_global();
    if(fd_idx == -1) {
        printk("exceed max open files\n");
        return -1;
    }
    file_table[fd_idx].fd_inode = inode_open(cur_part, inode_no);
    file_table[fd_idx].fd_pos = 0;//每次打开文件，要将fd_pos还原为0,即让文件内的指针指向开头
    file_table[fd_idx].fd_flag = flag;

    bool* write_deny = &file_table[fd_idx].fd_inode->write_deny;

    if(flag & O_WRONLY || flag & O_RDWR) {
        //进入临界区先关中断
        enum intr_status old_status = intr_disable();
        if(!(*write_deny)) {
            *write_deny = true;//避免多个进程同时写此文件
            intr_set_status(old_status);//恢复中断
        }else {
            intr_set_status(old_status);
            printk("file can't be write now, try again later\n");
            return -1;
        }
    }
    //若是读文件或创建文件,不用理会 write_deny,保持默认

    return pcb_fd_install(fd_idx);
}

//关闭文件
int32_t file_close(struct file* file) {
    if(file == NULL) {
        return -1;
    }
    file->fd_inode->write_deny = false;
    inode_close(file->fd_inode);
    file->fd_inode = NULL;  //使文件结构可用
    return 0;
}

//把buf中的count个字节写入file
int32_t file_write(struct file* file, const void* buf, uint32_t count) {
    if((file->fd_inode->i_size + count) > (BLOCK_SIZE * 140)) {
        printk("exceed max file_size 71680 bytes, write file failed\n");
        return -1;
    }
    uint8_t* io_buf = sys_malloc(512);
    if (io_buf == NULL) {
        printk("file_write: sys_malloc for io_buf fsiled\n");
        return -1;
    }
    uint32_t* all_blocks = (uint32_t*)sys_malloc(BLOCK_SIZE + 48);
    //用来记录文件所有块地址
    if(all_blocks == NULL) {
        printk("file_write:sys_malloc for all_blocks failed\n");
        return -1;
    }

    const uint8_t* src = buf;//指向待写入的数据
    uint32_t bytes_written = 0;//用来记录已写入的数据大小
    uint32_t size_left = count;//用来记录未写入数据大小
    int32_t block_lba = -1;//块地址
    uint32_t block_bitmap_idx = 0;

    uint32_t sec_idx;//索引扇区
    uint32_t sec_lba;//扇区地址
    uint32_t sec_off_bytes;//扇区内字节偏移量
    uint32_t sec_left_bytes;//扇区内剩余字节量
    uint32_t chunk_size;//每次写入硬盘的数据块大小
    int32_t indirect_block_table;//用来获取一级间接表地址
    uint32_t block_idx;//块索引

    //判断文件是否是第一次写
    if(file->fd_inode->i_sectors[0] == 0) {
        block_lba = block_bitmap_alloc(cur_part);
        if(block_lba == -1) {
            printk("file_write: block_bitmap_alloc failed\n");
            return -1;
        }
        file->fd_inode->i_sectors[0] = block_lba;

        //每分配一个块就将位图同步到硬盘
        block_bitmap_idx = block_lba - cur_part->sb->data_start_lba;
        ASSERT(block_bitmap_idx != 0);
        bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);
    }

    //写入count个字节前，该文件已经占用的块数
    uint32_t file_has_used_blocks = file->fd_inode->i_size / BLOCK_SIZE + 1;

    //存储count字节后该文件将占用的块数
    uint32_t file_will_use_blocks = (file->fd_inode->i_size + count) / BLOCK_SIZE + 1;
    ASSERT(file_will_use_blocks <= 140);

    //通过此增量判断是否需要分配扇区
    uint32_t add_blocks = file_will_use_blocks - file_has_used_blocks;

    if(add_blocks == 0) {
        if(file_will_use_blocks <= 12) {
            block_idx = file_has_used_blocks - 1;
            all_blocks[block_idx] = file->fd_inode->i_sectors[block_idx];
        }else {
            ASSERT(file->fd_inode->i_sectors[12] != 0);
            indirect_block_table = file->fd_inode->i_sectors[12];
            ide_read(cur_part->my_disk, indirect_block_table, all_blocks + 12, 1);
        }
    }else {
        //若有增量，涉及到分配新扇区是否分配一级间接表

        //第一种情况:12个够用
        if(file_will_use_blocks <= 12) {
            //先将有剩余空间的可继续用的扇区地址写入all_blocks
            block_idx = file_has_used_blocks - 1;
            ASSERT(file->fd_inode->i_sectors[block_idx] != 0);
            all_blocks[block_idx] = file->fd_inode->i_sectors[block_idx];

            //再将未来要用的扇区分配好后写入all_blocks
            block_idx = file_has_used_blocks;
            while(block_idx < file_will_use_blocks) {
                block_lba = block_bitmap_alloc(cur_part);
                if(block_lba == -1) {
                    printk("file_write: block_bitmap_alloc for situation 1 failed\n");
                    return -1;
                }

                ASSERT(file->fd_inode->i_sectors[block_idx] == 0);
                //确保尚未分配扇区地址

                file->fd_inode->i_sectors[block_idx] = all_blocks[block_idx] = block_lba;

                //每分配一个块就将位图同步到硬盘
                block_bitmap_idx = block_lba - cur_part->sb->data_start_lba;
                bitmap_sync(cur_part, block_bitmap_alloc, BLOCK_BITMAP);

                block_idx++;
            }
        }else if(file_has_used_blocks <= 12 && file_will_use_blocks > 12) {
            //第二种情况：就数据在12个直接块内，新数据使用间接块

            block_idx = file_has_used_blocks - 1;//先将有剩余空间的可继续用的扇区地址收集到all_blocks
            all_blocks[block_idx] = file->fd_inode->i_sectors[block_idx];

            //创建一级间接表
            block_lba = block_bitmap_alloc(cur_part);
            if(block_lba == -1) {
                printk("file_write: block_bitmap_alloc for situation 2 failed\n");
                return -1;
            }

            ASSERT(file->fd_inode->i_sectors[12] == 0);

            //分配一级间接块索引
            indirect_block_table = file->fd_inode->i_sectors[12] = block_lba;

            block_idx = file_has_used_blocks;

            while(block_idx < file_will_use_blocks) {
                block_lba = block_bitmap_alloc(cur_part);
                if(block_lba == -1) {
                    printk("file_write: block_bitmap_alloc for situation 2 failed\n");
                    return -1;
                }

                if(block_idx < 12) {
                    //新创建的0～11块直接存入all_blocks数组
                    ASSERT(file->fd_inode->i_sectors[block_idx] == 0);

                    file->fd_inode->i_sectors[block_idx] = all_blocks[block_idx] = block_lba;
                }else {
                    //间接块写入all_block数组中，待全部分配完成后一次性同步到硬盘
                    all_blocks[block_idx] = block_lba;
                }

                //每分配一个块就将位图同步到硬盘
                block_bitmap_idx = block_lba - cur_part->sb->data_start_lba;
                bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);

                block_idx++;//下一个新扇区
            }
            ide_write(cur_part->my_disk, indirect_block_table, all_blocks + 12, 1);//同步一级间接块表到硬盘
        }else if(file_has_used_blocks > 12) {
            //第三种情况：新数据占据间接块
            ASSERT(file->fd_inode->i_sectors[12] != 0);

            indirect_block_table = file->fd_inode->i_sectors[12];//获取一级间接块表地址

            //以使用的间接块也将被读入all_blocks,无需单独收录
            ide_read(cur_part->my_disk, indirect_block_table, all_blocks + 12, 1);//获取所有间接块地址

            block_idx = file_has_used_blocks;

            while(block_idx < file_will_use_blocks) {
                block_lba = block_bitmap_alloc(cur_part);
                if(block_lba == -1) {
                    printk("file_write: block_bitmap_alloc for situation 3 failed\n");
                    return -1;
                }
                all_blocks[block_idx++] = block_lba;

                //每分配一块就将位图同步到硬盘
                block_bitmap_idx = block_lba - cur_part->sb->data_start_lba;
                bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);
            }
            ide_write(cur_part->my_disk, indirect_block_table, all_blocks + 12, 1);//同步一级间接块表到硬盘
        }
    }

    //用到的块地址已经收集到all_blocks,下面开始写数据
    bool first_write_block = true;//含有剩余空间的块标识
    file->fd_pos = file->fd_inode->i_size - 1;

    while(bytes_written < count) {
        memset(io_buf, 0, BLOCK_SIZE);
        sec_idx = file->fd_inode->i_size / BLOCK_SIZE;
        sec_lba = all_blocks[sec_idx];
        sec_off_bytes = file->fd_inode->i_size % BLOCK_SIZE;
        sec_left_bytes = BLOCK_SIZE - sec_off_bytes;

        //判断此次写入硬盘数据大小'
        chunk_size = size_left < sec_left_bytes ? size_left : sec_left_bytes;
        if(first_write_block) {
            ide_read(cur_part->my_disk, sec_lba, io_buf, 1);
            first_write_block = false;
        }
        memcpy(io_buf + sec_off_bytes, src, chunk_size);
        ide_write(cur_part->my_disk, sec_lba, io_buf, 1);
        printk("file write at lba 0x%x\n", sec_lba);

        src += chunk_size;
        file->fd_inode->i_size += chunk_size;//更新文件大小
        file->fd_pos += chunk_size;
        bytes_written += chunk_size;
        size_left -= chunk_size;
    }
    inode_sync(cur_part, file->fd_inode, io_buf);
    sys_free(all_blocks);
    sys_free(io_buf);
    return bytes_written;
}

//从文件file中读取count个字节写入buf,返回读出的字节数，文件为返回-1
int32_t file_read(struct file* file, void* buf, uint32_t count) {
    uint8_t* buf_dst = (uint8_t*)buf;
    uint32_t size = count, size_left = size;

    if((file->fd_pos + count) > file->fd_inode->i_size) {
        size = file->fd_inode->i_size - file->fd_pos;
        size_left = size;
        if(size == 0) {
            return -1;
        }
    }

    uint8_t* io_buf = sys_malloc(BLOCK_SIZE);
    if(io_buf == NULL) {
        printk("file_read: sys_malloc for io_buf failed\n");
    }

    uint32_t* all_blocks = (uint32_t*)sys_malloc(BLOCK_SIZE + 48);//记录文件所有块地址
    if(all_blocks == NULL) {
        printk("file_read: sys_malloc for all_blocks failed\n");
        return -1;
    }

    uint32_t block_read_start_idx = file->fd_pos / BLOCK_SIZE;//数据所在块起始地址
    uint32_t block_read_end_idx = (file->fd_pos + size) / BLOCK_SIZE;//数据所在块终止地址
    uint32_t read_blocks = block_read_start_idx - block_read_end_idx;

    ASSERT(block_read_start_idx < 139 && block_read_end_idx < 139);

    int32_t indirect_block_table;   //用来获取一级间接块表地址
    uint32_t block_idx; //获取待读的块地址

    //开始构建all_blocks块地址数组
    if(read_blocks == 0){
        //在同一扇区读数据
        ASSERT(block_read_end_idx == block_read_start_idx);
        if(block_read_end_idx < 12) {
            //在直接块内
            block_idx = block_read_end_idx;
            all_blocks[block_idx] = file->fd_inode->i_sectors[block_idx];
        }else {
            //用到了一级间接块，读进来
            indirect_block_table = file->fd_inode->i_sectors[12];
            ide_read(cur_part->my_disk, indirect_block_table, all_blocks + 12, 1);
        }
    }else {//读多个块
        //第一种情况：起始块和终止块属于直接块
        if(block_read_end_idx < 12) {
            block_idx = block_read_start_idx;
            while(block_idx <= block_read_end_idx) {
                all_blocks[block_idx] = file->fd_inode->i_sectors[block_idx];
                block_idx++;
            }
        }else if(block_read_start_idx < 12 && block_read_end_idx >= 12) {
            //第二种情况：待读入的数据跨越直接块和间接块
            //先将直接块地址写入all_blocks
            block_idx = block_read_start_idx;
            while(block_idx < 12) {
                all_blocks[block_idx] = file->fd_inode->i_sectors[block_idx];
                block_idx++;
            }
            ASSERT(file->fd_inode->i_sectors[12] != 0);

            indirect_block_table = file->fd_inode->i_sectors[12];
            ide_read(cur_part->my_disk, indirect_block_table, all_blocks + 12, 1);//写入到地13个位置
        }else{
            //第三种情况:数据在间接块中
            ASSERT(file->fd_inode->i_sectors[12] != 0);
            indirect_block_table = file->fd_inode->i_sectors[12];
            ide_read(cur_part->my_disk, indirect_block_table, all_blocks + 12, 1);
        }
    }

    //用到的块地址已经收集到all_blocks中，开始读数据
    uint32_t sec_idx, sec_lba, sec_off_bytes, sec_left_bytes, chunk_size;
    uint32_t bytes_read = 0;
    while(bytes_read < size) {
        //知道读完为止
        sec_idx = file->fd_pos / BLOCK_SIZE;
        sec_lba = all_blocks[sec_idx];
        sec_off_bytes = file->fd_pos % BLOCK_SIZE;
        sec_left_bytes = BLOCK_SIZE - sec_off_bytes;
        chunk_size = size_left < sec_left_bytes ? size_left : sec_left_bytes;//待读入的数据大小

        memset(io_buf, 0, BLOCK_SIZE);
        ide_read(cur_part->my_disk, sec_lba, io_buf, 1);
        memcpy(buf_dst, io_buf + sec_off_bytes, chunk_size);

        buf_dst += chunk_size;
        file->fd_pos += chunk_size;
        bytes_read += chunk_size;

        size_left -= chunk_size;

    }

    sys_free(all_blocks);
    sys_free(io_buf);
    return bytes_read;


}