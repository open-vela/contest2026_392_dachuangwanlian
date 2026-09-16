---------------------------------------------------------
1.tool工具命令
	复用--addr参数，作为相对EMMC虚拟地址的偏移量使用，有如下规定：
	A. 定义EMMC虚拟地址为0x3，复用DEV_TYPE_EMMC，具体如下所示：
	#define EMMC_ADDRESS DEV_TYPE_EMMC //标识emmc地址：
	#define DEV_TYPE_EMMC 0x3
	B.偏移量offset为相对EMMC虚拟地址的偏移量
	C.addr_value = 0x3 + offset	
	注意：当emmc以block为单位进行存储时，offset是相对block的偏移量，即：512b的偏移量是1，1024b的偏移量为2，依次类推。

(1).在emmc上下载一个bin文件时，假设: 相对EMMC虚拟地址的偏移量offset = 0x10000，addr_value = 0x3 + offset = 0x10003
	则命令如下：	
	dldtool.exe  10  emmc_programmer.bin  --addr 0x10003  1.bin 

(2).在emmc上下载多个bin文件，即1.bin，2.bin时，假设：1.bin下载emmc位置0x10000，2.bin下载位置为0x20000，
	则命令如下：
	dldtool.exe  10  emmc_programmer.bin --addr 0x10003  1.bin --addr 0x20003  2.bin

	假设，1.bin与2.bin连续下载时，可共有一个偏移量， offset = 0x10000，命令如下：
	dldtool.exe  10  emmc_programmer.bin --addr 0x10003  1.bin --addr 0x10003  2.bin

	注意：EMMC块存储特性。
---------------------------------------------------------
2.引脚配置
(1)reset HAL_GPIO_PIN_P1_4
	电压输出: 1.8v
(2)供电1 PMU_GPIO_PIN_P5_6
	电压输出: 1.8v
(3)供电2 PMU_GPIO_PIN_P6_5
	电压输出: 1.8v
---------------------------------------------------------
