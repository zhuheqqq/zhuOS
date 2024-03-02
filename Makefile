B=boot
K=kernel
D=device
T=thread
U=userprog
L=lib
F=fs
S=shell
LU=lib/user
LK=lib/kernel

# BOCHS相关参数
BOCHS_PATH=/home/zhuheqin/Desktop/bochs/bin/bochs
//BOCHS_GDB_PATH=/home/forrest/bochs/bin/bochs
BOCHS_PORt="target remote 127.0.0.1:1234"
BOCHS_GDB_FLAG='gdbstub:enabled=1,port=1234,text_base=0,data_base=0,bss_base=0'


K_OBJS=$K/main.o \
	   $K/init.o \
	   $K/interrupt.o\
	   #$K/debug.o \
	   $K/memory.o 



#D_OBJS=$D/keyboard.o \
       $D/timer.o  \
	   $D/ioqueue.o \
	   $D/ide.o \
	   $D/console.o
	   

#_OBJS=$T/sync.o \
	   $T/thread.o 


#U_OBJS=$U/tss.o \
	   $U/process.o \
	   $U/syscall_init.o  \
	   $U/fork.o \
	   $U/exec.o \
	   $U/wait_exit.o 



#F_OBJS=$F/fs.o \
	   $F/inode.o \
	   $F/dir.o  \
	   $F/file.o

#_OBJS=$S/shell.o \
	   $S/buildin_cmd.o \
	   $S/pipe.o 


#L_OBJS=$L/stdio.o \
       $L/stdint.o \
	   $L/string.o 

#LU_OBJS=${LU}/syscall.o \
		${LU}/assert.o

#LK_OBJS=${LK}/stdio_kernel.o  \
		${LK}/bitmap.o \
		${LK}/list.o \
		${LK}/io.o \
		${LK}/print.o
	



	
GCC_FLAGS = -c -Wall -m32 -ggdb  \
-nostdinc  -fno-builtin -fno-stack-protector -mno-sse -g 

OBJS=${K_OBJS}   \
	 ${D_OBJS}   \
	 ${T_OBJS}   \
	 ${U_OBJS}   \
	 ${LK_OBJS}  \
	 ${LU_OBJS}  \
	 ${L_OBJS}  \
	 ${F_OBJS} \
	 ${S_OBJS}

include= -I $K -I $D -I $T -I $U -I $L -I $F -I $S -I ${LK} -I ${LU} 




build:${OBJS}
	nasm -I $B/include -o $B/mbr.bin   $B/mbr.asm 
	nasm -I $B/include -o $B/loader.bin $B/loader.asm 
	nasm -f elf -o $K/kernel.o $K/kernel.asm 
#	nasm -f elf -o $T/switch.o $T/switch.asm
	ld -m elf_i386 -T kernel.ld -o kernel.bin ${OBJS} $K/kernel.o  

$K/%.o:$K/%.c 
	gcc ${include} ${GCC_FLAGS} -o $@ $^ 

$D/%.o:$D/%.c
	gcc ${include} ${GCC_FLAGS} -o $@ $^ 

$T/%.o:$T/%.c
	gcc ${include} ${GCC_FLAGS} -o $@ $^ 

$U/%.o:$U/%.c
	gcc ${include} ${GCC_FLAGS} -o $@ $^

$L/%.o:$L/%.c
	gcc ${include} ${GCC_FLAGS} -o $@ $^
	
$F/%.o:$F/%.c
	gcc ${include} ${GCC_FLAGS} -o $@ $^

$S/%.o:$S/%.c
	gcc ${include} ${GCC_FLAGS} -o $@ $^

${LK}/%.o:${LK}/%.c
	gcc ${include} ${GCC_FLAGS} -o $@ $^

${LU}/%.o:${LU}/%.c
	gcc ${include} ${GCC_FLAGS} -o $@ $^



dd: build
	dd if=/dev/zero of=$B/boot.img bs=1M count=60
	dd if=$B/mbr.bin of=$B/boot.img bs=512 count=1 conv=notrunc
	dd if=$B/loader.bin of=$B/boot.img bs=512 count=4 seek=2 conv=notrunc
	dd if=kernel.bin of=$B/boot.img bs=512 count=200 seek=9 conv=notrunc




run:dd 
	# @ rm -rf hd60M.img 
	# sh partition.sh 
	# cd command && sh compile.sh
	${BOCHS_PATH} -qf bochsrc.disk  
	make clean
	
run_gdb:dd
	# @ rm -rf hd60M.img 
	# sh partition.sh 
	# cd command && sh compile.sh
	${BOCHS_GDB_PATH} -qf bochsrc.disk  ${BOCHS_GDB_FLAG} & 
	gdb ./kernel.bin -ex ${BOCHS_PORt}
	pkill bochs
	make  clean
	


clean:
	@ rm -rf *.lock
	@ rm -rf boot/*.lock
	@ rm -f  kernel/*.o
	@ rm -f  device/*.o
	@ rm -f  thread/*.o
	@ rm -f  userprog/*.o
	@ rm -f  boot/*.bin
	@ rm -f  *.bin
	@ rm -f  boot/*.img
	@ rm -rf lib/user/*.o
	@ rm -rf lib/kernel/*.o
	@ rm -rf lib/*.o
	@ rm -rf fs/*.o
	@ rm -rf command/*.o