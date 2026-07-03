#include "my_jump.h"

typedef void (*Pfunction)(void);

uint32_t first_page_address = 0x08008000;
uint32_t pages_to_erase = 240;

void Jump_To_App(void)
{
    uint32_t app_stack = *(__IO uint32_t*)APP_START;
    // 检查栈顶指针是否在 64KB SRAM 范围内 (STM32F103ZET6)
    if (app_stack >= 0x20000000 && app_stack < 0x20010000)
    {
        __disable_irq();                // 关闭中断
        SCB->VTOR = APP_START;          // 设置向量表偏移
        __DSB();                        // 数据同步屏障
        __ISB();                        // 指令同步屏障
        __set_MSP(app_stack);           // 设置主栈指针
        // 跳转到应用程序
        ((void (*)(void))(*((__IO uint32_t*)(APP_START + 4))))();
    }
}

int8_t Erase_Apparea(void)
{
    __disable_irq();
    HAL_FLASH_Unlock();
    
    FLASH_EraseInitTypeDef erase = {0};
    erase.TypeErase   = FLASH_TYPEERASE_PAGES;
    erase.PageAddress = first_page_address;
    erase.NbPages     = pages_to_erase;          // 480KB
    
    uint32_t page_error = 0;
    if (HAL_FLASHEx_Erase(&erase, &page_error) != HAL_OK) {
        printf("Erase Err at page address: 0x%lx\r\n", page_error);
        HAL_FLASH_Lock();
        __enable_irq();
        return -1;
    }
    
    printf("Erase OK\r\n");
    HAL_FLASH_Lock();
    __enable_irq();
    return 0;
}

