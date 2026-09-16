---------------------------------------------------------
programmer1502x_nand_1Gb.bin 仅用来向 nandflash 烧录文件, 不能用于烧录RTOS
（仅针对 block page = 64, page size = 2048）
（ total_size = 134217728 block_size = 131072 page_size = 2048）

1. dldtool 工具命令

复用 --addr 参数, 作为相对 nandflash 虚拟地址的偏移量使用, 对烧录地址及烧录区间大小, 有如下规定：
A. addr_value = dev_base_addr + offset
PS: 输入数值支持十进制及十六进制
B. 设备虚拟地址 dev_base_addr = 0x3
C. 偏移量 offset 为相对 nandflash 虚拟地址的偏移量
PS: offset 要求以 block 对齐, 单位为 page, 即如, block page = 64, 则 offset 为 64(0x40)的整数倍
PS: 烧录数据大小要求以 page 对齐, 单位为 byte, 即如, page size = 2048, 则 数据大小为 2048(0x800)的整数倍
    因考虑坏块因素, nandflash 推荐实际使用2/3的全盘空间大小用于数据存储，即128MB的总大小推荐实际使用80+MB
    BES_NAND_DATA_SIZE 用于定义 data_nand 数据分区大小

举例: （以 yaffs2 文件系统为例）向 nandflash 起始地址烧录一个bin文件时, 则命令如下：
dldtool.exe 6 programmer1502x_nand_1Gb.bin --addr 0x3 yaffs2.bin

PS: 如需在 NuttX 中启用 yaffs2 文件系统需使能 CONFIG_FS_YAFFS

2. yaffs2 文件系统镜像的制作
在 ./rtos/nuttx/fs/yaffs/yaffs/utils 路径下执行 make 命令，编译 yaffs2 镜像制作命令 mkyaffs2image
mkdir yaffs2_fs 创建 yaffs2_fs 文件夹，并将需要装入镜像的目录及文件放入其中，执行如下命令制作镜像
./mkyaffs2image yaffs2_fs/ yaffs2.bin 2048

mkyaffs2image: image building tool for YAFFS2 built Sep 27 2024
usage: mkyaffs2image dir image_file chunkSize [convert]
        dir        the directory tree to be converted
        image_file the output file to hold the image
        chunkSize  the size of chunkSize
        'convert'  produce a big-endian image from a little-endian machine

3. 在 ap 的 menuconfig 中使能 CONFIG_BES_NAND_FLASH 重新编译 nuttx_ap.bin 并烧录以启用 nandflash

4. 完成 NuttX 镜像烧录, 设备启动后, 系统自动注册 /dev/bes_nand 设备及 /dev/data_nand 分区，并可使用如下命令将其挂载到 /data_nand 路径
mount -t yaffs /dev/data_nand /data_nand

5. yaffs2 文件系统验证
ls /data_nand 命令查看 /data_nand 下目录结构，可以看到烧录的文件系统镜像中包含的目录及文件
或使用 echo "123" > /data_nand/test.txt 命令在 /data_nand 下创建一个文本内容为 123 的文件 test.txt
使用 cat /data_nand/test.txt 查看文件内容
---------------------------------------------------------
