### 本地运行zhuOS

在本地需要有bochs,因为此文档是为了 我自己 忘记 zhuOS 的运行方式而做，所以方式不具有普遍性


在~/clone/zhuOS中
```c
make all
```

在 本地 ~/Desktop/bochs目录下
```c
/usr/bin/bochs -f bochsrc.disk
```
然后回车 输入 c（即continue）即可进入运行界面