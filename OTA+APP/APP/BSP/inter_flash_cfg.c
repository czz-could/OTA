#include "inter_flash_cfg.h"
#include <string.h>
#include <stdio.h>
#include "inter_flashif.h"
#include "usart.h"
static inter_flash_cfg_param_typeDef flash_cfg_param;

/**
 * @brief 读取参数扇区数据
 * 
 * @return uint8_t 
 */
uint8_t inter_flash_cfg_get_param_sector()
{
    //读取系统配置区参数
    inter_flashif_read_page(INTER_FLASH_PARAM_ADDR, (uint8_t *)&flash_cfg_param, sizeof(flash_cfg_param));
    //校验魔术数字
    if((flash_cfg_param.magic[0] != 0xAA) || (flash_cfg_param.magic[1] != 0xBB) ||
        (flash_cfg_param.magic[2] != 0xCC) || (flash_cfg_param.magic[3] != 0xDD))
    {
        printf("flash magic error\r\n");
        return 1;           //扇区错误
    }

    //计算校验和
    uint8_t checksum = inter_flash_checksum((uint8_t *)&flash_cfg_param, sizeof(flash_cfg_param) - 3);
    if(checksum != flash_cfg_param.checksum)
    {
        printf("flash checksum error\r\n");
        return 2;           //校验和错误
    }

    return 0;
}


/**
 * @brief 获取Flash升级标志
 * 
 * @return uint8_t 
 */
int8_t inter_flash_cfg_get_app_update_flag(void)
{
    uint8_t ret = inter_flash_cfg_get_param_sector();
    if(ret != 0)            //获取参数
    {
        return -1;
    }

    //升级标志获取
	return flash_cfg_param.ota_flag;
}

/**
 * @brief 写升级标志
 * 
 * @param flag 
 * @return uint8_t 
 */
int8_t inter_flash_cfg_set_app_update_flag(uint8_t flag)
{
    uint8_t ret = inter_flash_cfg_get_param_sector();
    if(ret != 0)            //获取参数
    {
        printf("flash get update flag error\r\n");
        return -1;
    }
    flash_cfg_param.ota_flag = flag;

    //计算校验和
    flash_cfg_param.checksum = inter_flash_checksum((uint8_t *)&flash_cfg_param, sizeof(flash_cfg_param) - 3);

    //写回参数
    inter_flashif_smart_write_page(INTER_FLASH_PARAM_ADDR, (uint32_t *)&flash_cfg_param, sizeof(flash_cfg_param)/sizeof(uint32_t));

	return 0;
}

static void inter_flash_cfg_write_default(void)
{
    inter_flash_cfg_param_typeDef default_param;

    // 设置默认值
    default_param.magic[0] = 0xAA;
    default_param.magic[1] = 0xBB;
    default_param.magic[2] = 0xCC;
    default_param.magic[3] = 0xDD;
    default_param.ota_bin_version = 0;
    default_param.ota_flag = 0;          // 默认不需要升级
    default_param.checksum = 0;          // 先清零，稍后计算
    default_param.format[0] = 0;
    default_param.format[1] = 0;

    // 计算校验和（排除最后3字节：checksum本身和2字节format）
    default_param.checksum = inter_flash_checksum((uint8_t*)&default_param, sizeof(default_param) - 3);

    // 写入Flash（会自动擦除所在页）
    inter_flashif_smart_write_page(INTER_FLASH_PARAM_ADDR, (uint32_t*)&default_param, sizeof(default_param) / 4);
}



/**
 * @brief 内部flash参数初始化
 * 
 * @return uint8_t 
 */
uint8_t inter_flash_cfg_init(void)
{
    // 检查结构体大小是否为4的倍数（Flash写入要求）
    if (sizeof(inter_flash_cfg_param_typeDef) % 4 != 0) {
        while(1) {
            printf("flash cfg param size error\r\n");
            HAL_Delay(1000);
        }
    }

    // 尝试读取参数区
    uint8_t ret = inter_flash_cfg_get_param_sector();
    if (ret != 0) {
        // 参数区无效（magic错误或checksum错误），则写入默认参数
        printf("Param sector invalid, writing default...\r\n");
        inter_flash_cfg_write_default();

        // 重新读取验证
        ret = inter_flash_cfg_get_param_sector();
        if (ret != 0) {
            // 写入后仍然失败，说明Flash硬件问题或地址错误
            while(1) {
                printf("Failed to init param sector!\r\n");
                HAL_Delay(1000);
            }
        } else {
            printf("Param sector initialized successfully.\r\n");
        }
    }
    return 0;
}


