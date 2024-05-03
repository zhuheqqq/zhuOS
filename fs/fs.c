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
            break;
            //其余为打开文件
        default:
            //printf("default start.....\n");
            fd = file_open(inode_no, flags);
    }

    put_str("sys_open end....\n");
    return fd;//pcb->fd_table数组的元素下标

}

//将文件描述符转化为文件表的下标
static uint32_t fd_local2global(uint32_t local_fd) {
    struct task_struct* cur = running_thread();
    int32_t global_fd = cur->fd_table[local_fd];
    ASSERT(global_fd >= 0 && global_fd < MAX_FILE_OPEN);
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

//将buf中连续count个字节写入文件描述符fd
int32_t sys_write(int32_t fd, const void* buf, uint32_t count) {
    if(fd < 0) {
        printk("sys_write: fd error\n");
        return -1;
    }
    if(fd == stdout_no) {
        char tmp_buf[1024] = {0};
        memcpy(tmp_buf, buf, count);
        console_put_str(tmp_buf);
        return count;
    }
    uint32_t _fd = fd_local2global(fd);
    struct file* wr_file = &file_table[_fd];
    if(wr_file->fd_flag & O_WRONLY || wr_file->fd_flag & O_RDWR) {
        uint32_t bytes_written = file_write(wr_file, buf, count);
        return bytes_written;
    }else {
        console_put_str("sys_write: not allowed to write file without flag O_RDWR or O_WRONLY\n");
        return -1;
    }
}

//从文件描述符fd中指向的文件读取count个字节到buf,成功返回读出的字节数
int32_t sys_read(int32_t fd, void* buf, uint32_t count) {
    if(fd < 0) {
        printk("sys_read: fd error\n");
        return -1;
    }

    ASSERT(buf != NULL);
    uint32_t _fd = fd_local2global(fd);
    return file_read(&file_table[_fd], buf, count);
}

//重置用于文件读写操作的偏移指针，成功返回新的偏移量
int32_t sys_lseek(int32_t fd, int32_t offset, uint8_t whence) {
    if(fd < 0) {
        printk("sys_lseek: fd error");
        return -1;
    }
    ASSERT(whence > 0 && whence < 4);
    uint32_t _fd = fd_local2global(fd);
    struct file* pf = &file_table[_fd];
    int32_t new_pos = 0;
    int32_t file_size = (int32_t)pf->fd_inode->i_size;

    switch (whence) {
      /* SEEK_SET 新的读写位置是相对于文件开头再增加offset个位移量 */
        case SEEK_SET:
	        new_pos = offset;
	        break;

      /* SEEK_CUR 新的读写位置是相对于当前的位置增加offset个位移量 */
        case SEEK_CUR:	// offse可正可负
	        new_pos = (int32_t)pf->fd_pos + offset;
	        break;

      /* SEEK_END 新的读写位置是相对于文件尺寸再增加offset个位移量 */
        case SEEK_END:	   // 此情况下,offset应该为负值
	        new_pos = file_size + offset;
    }
   if (new_pos < 0 || new_pos > (file_size - 1)) {	 
        return -1;
   }
   pf->fd_pos = new_pos;
   return pf->fd_pos;
}

/* 删除文件(非目录),成功返回0,失败返回-1 */
int32_t sys_unlink(const char* pathname) {
   ASSERT(strlen(pathname) < MAX_PATH_LEN);

   /* 先检查待删除的文件是否存在 */
   struct path_search_record searched_record;
   memset(&searched_record, 0, sizeof(struct path_search_record));
   int inode_no = search_file(pathname, &searched_record);
   ASSERT(inode_no != 0);
   if (inode_no == -1) {
      printk("file %s not found!\n", pathname);
      dir_close(searched_record.parent_dir);
      return -1;
   }
   if (searched_record.file_type == FT_DIRECTORY) {
      printk("can`t delete a direcotry with unlink(), use rmdir() to instead\n");
      dir_close(searched_record.parent_dir);
      return -1;
   }

   /* 检查是否在已打开文件列表(文件表)中 */
   uint32_t file_idx = 0;
   while (file_idx < MAX_FILE_OPEN) {
      if (file_table[file_idx].fd_inode != NULL && (uint32_t)inode_no == file_table[file_idx].fd_inode->i_no) {
	 break;
      }
      file_idx++;
   }
   if (file_idx < MAX_FILE_OPEN) {
      dir_close(searched_record.parent_dir);
      printk("file %s is in use, not allow to delete!\n", pathname);
      return -1;
   }
   ASSERT(file_idx == MAX_FILE_OPEN);
   
   /* 为delete_dir_entry申请缓冲区 */
   void* io_buf = sys_malloc(SECTOR_SIZE + SECTOR_SIZE);
   if (io_buf == NULL) {
      dir_close(searched_record.parent_dir);
      printk("sys_unlink: malloc for io_buf failed\n");
      return -1;
   }

   struct dir* parent_dir = searched_record.parent_dir;  
   delete_dir_entry(cur_part, parent_dir, inode_no, io_buf);
   inode_release(cur_part, inode_no);
   sys_free(io_buf);
   dir_close(searched_record.parent_dir);
   return 0;   // 成功删除文件 
}


/*
1.确认待创建的新目录在文件系统不存在
2.为新目录创建inode
3.为新目录分配1个块存储该目录中的目录项
4.在新目录中创建.和..
5.在新目录的父目录中添加新目录的目录项
6.将以上同步到硬盘
*/
/* 创建目录pathname,成功返回0,失败返回-1 */
int32_t sys_mkdir(const char *pathname)
{
    uint8_t rollback_step = 0; // 用于操作失败时回滚各资源状态
    void *io_buf = sys_malloc(SECTOR_SIZE * 2);
    if (io_buf == NULL)
    {
        printk("sys_mkdir: sys_malloc for io_buf failed\n");
        return -1;
    }

    struct path_search_record searched_record;
    memset(&searched_record, 0, sizeof(struct path_search_record));
    int inode_no = -1;
    inode_no = search_file(pathname, &searched_record);
    if (inode_no != -1)
    { // 如果找到了同名目录或文件,失败返回
        printk("sys_mkdir: file or directory %s exist!\n", pathname);
        rollback_step = 1;
        goto rollback;
    }
    else
    { // 若未找到,也要判断是在最终目录没找到还是某个中间目录不存在
        uint32_t pathname_depth = path_depth_cnt((char *)pathname);
        uint32_t path_searched_depth = path_depth_cnt(searched_record.searched_path);
        /* 先判断是否把pathname的各层目录都访问到了,即是否在某个中间目录就失败了 */
        if (pathname_depth != path_searched_depth)
        { // 说明并没有访问到全部的路径,某个中间目录是不存在的
            printk("sys_mkdir: can`t access %s, subpath %s is`t exist\n", pathname, searched_record.searched_path);
            rollback_step = 1;
            goto rollback;
        }
    }

    struct dir *parent_dir = searched_record.parent_dir;
    /* 目录名称后可能会有字符'/',所以最好直接用searched_record.searched_path,无'/' */
    char *dirname = strrchr(searched_record.searched_path, '/') + 1;

    inode_no = inode_bitmap_alloc(cur_part);
    if (inode_no == -1)
    {
        printk("sys_mkdir: allocate inode failed\n");
        rollback_step = 1;
        goto rollback;
    }

    struct inode new_dir_inode;
    inode_init(inode_no, &new_dir_inode); // 初始化i结点

    uint32_t block_bitmap_idx = 0; // 用来记录block对应于block_bitmap中的索引
    int32_t block_lba = -1;
    /* 为目录分配一个块,用来写入目录.和.. */
    block_lba = block_bitmap_alloc(cur_part);
    if (block_lba == -1)
    {
        printk("sys_mkdir: block_bitmap_alloc for create directory failed\n");
        rollback_step = 2;
        goto rollback;
    }
    new_dir_inode.i_sectors[0] = block_lba;
    /* 每分配一个块就将位图同步到硬盘 */
    block_bitmap_idx = block_lba - cur_part->sb->data_start_lba;
    ASSERT(block_bitmap_idx != 0);
    bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);

    /* 将当前目录的目录项'.'和'..'写入目录 */
    memset(io_buf, 0, SECTOR_SIZE * 2); // 清空io_buf
    struct dir_entry *p_de = (struct dir_entry *)io_buf;

    /* 初始化当前目录"." */
    memcpy(p_de->filename, ".", 1);
    p_de->i_no = inode_no;
    p_de->f_type = FT_DIRECTORY;

    p_de++;
    /* 初始化当前目录".." */
    memcpy(p_de->filename, "..", 2);
    p_de->i_no = parent_dir->inode->i_no;
    p_de->f_type = FT_DIRECTORY;
    ide_write(cur_part->my_disk, new_dir_inode.i_sectors[0], io_buf, 1);

    new_dir_inode.i_size = 2 * cur_part->sb->dir_entry_size;

    /* 在父目录中添加自己的目录项 */
    struct dir_entry new_dir_entry;
    memset(&new_dir_entry, 0, sizeof(struct dir_entry));
    create_dir_entry(dirname, inode_no, FT_DIRECTORY, &new_dir_entry);
    memset(io_buf, 0, SECTOR_SIZE * 2); // 清空io_buf
    if (!sync_dir_entry(parent_dir, &new_dir_entry, io_buf))
    { // sync_dir_entry中将block_bitmap通过bitmap_sync同步到硬盘
        printk("sys_mkdir: sync_dir_entry to disk failed!\n");
        rollback_step = 2;
        goto rollback;
    }

    /* 父目录的inode同步到硬盘 */
    memset(io_buf, 0, SECTOR_SIZE * 2);
    inode_sync(cur_part, parent_dir->inode, io_buf);

    /* 将新创建目录的inode同步到硬盘 */
    memset(io_buf, 0, SECTOR_SIZE * 2);
    inode_sync(cur_part, &new_dir_inode, io_buf);

    /* 将inode位图同步到硬盘 */
    bitmap_sync(cur_part, inode_no, INODE_BITMAP);

    sys_free(io_buf);

    /* 关闭所创建目录的父目录 */
    dir_close(searched_record.parent_dir);
    return 0;

/*创建文件或目录需要创建相关的多个资源,若某步失败则会执行到下面的回滚步骤 */
rollback: // 因为某步骤操作失败而回滚
    switch (rollback_step)
    {
    case 2:
        bitmap_set(&cur_part->inode_bitmap, inode_no, 0); // 如果新文件的inode创建失败,之前位图中分配的inode_no也要恢复
    case 1:
        /* 关闭所创建目录的父目录 */
        dir_close(searched_record.parent_dir);
        break;
    }
    sys_free(io_buf);
    return -1;
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



