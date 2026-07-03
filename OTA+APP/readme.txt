APP的flash存储起始地址：	                 	0x8008000      	大小：0x78000  480k
BOOTLOADER的FLASH存储起始地址：        	0x8000000      	大小：0x5FFF   23k
升级结构体参数的保存flash存储的起始地址：	0x8006000	大小：0x8007FFF
实现通过蓝牙配合串口调试助手（下放OTA，注意要有回车，进入等待升级）+Xshell8进行OTA升级，或者使用E:\CZZ\STM32_OTA\BOOTLOADER\OTA+APP\OTA
里面的Ymodem.html进行一键升级






c8t6  20k  sram   64k  flash
APP的flash存储起始地址：	                 	0x8004000      	大小：0xc000       30k
BOOTLOADER的FLASH存储起始地址：        	0x8000000      	大小：0x3FFF       15k
升级结构体参数的保存flash存储的起始地址：	0x8006000	大小：0x8007FFF

修改后
BOOTLOADER的FLASH存储起始地址：        	0x8000000      	大小：0x3FFF       16k
升级结构体参数的保存flash存储的起始地址：	0x8004000	大小：0x7FF         2k
APP的flash存储起始地址：	                 	0x8004800      	大小：0x9FFF       46k


64-30=34
my_jump.c需要改flash跟sram