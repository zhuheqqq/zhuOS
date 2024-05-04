#ifndef __USERPROG_SYSCALLINIT_H
#define __USERPROG_SYSCALLINIT_H
#include "stdint.h"
#include "thread.h"
void syscall_init(void);
uint32_t sys_getpid(void);
void* sys_malloc(uint32);
void sys_free(void*);
pid_t sys_fork(void);
#endif