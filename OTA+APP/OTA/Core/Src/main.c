/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "header.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/* ######################### 功能说明 #########################
   1. 正常运行 APP 业务逻辑
   2. 串口收到指令 "OTA" → 立刻停止任务 → 进入 Ymodem 升级
   3. 升级完成自动跳转新 APP
############################################################# */

// 串口接收缓存
#define RX_BUF_SIZE 128

uint8_t  uart_rx_buf[RX_BUF_SIZE];  // 接收缓存
uint16_t uart_rx_cnt = 0;           // 接收计数
uint8_t uart_data = 0;
uint8_t OTA_FLAG = 0;

// 函数前置声明
void System_EnterUpgradeMode(void);
void OTA_CheckCommand(uint8_t *data, uint16_t len);
void Error_Handler(void);
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_USART1_UART_Init();
  /* USER CODE BEGIN 2 */
	// 初始化参数区（校验魔术字和校验和）
	if (inter_flash_cfg_init() != 0) {
			// 参数区损坏，可能需要擦除重建，这里简单进入升级模式
			printf("Param sector error, enter upgrade...\r\n");
			System_EnterUpgradeMode();
	}
	// 检查 OTA 升级标志
	int8_t ota_flag = inter_flash_cfg_get_app_update_flag();
	if (ota_flag == 1) {
			printf("OTA flag set, enter upgrade mode...\r\n");
			System_EnterUpgradeMode();   // 升级成功后清除标志并跳转 APP
	} else {
			printf("Jump to application...\r\n");
			Jump_To_App();
	}
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
		
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */
// ###########################################################################
// ######################### 核心：进入升级模式 ##############################
// ###########################################################################
void System_EnterUpgradeMode(void)
{
    printf("\r\n=========================================\r\n");
    printf("     IAP Bootloader - Ymodem Receive\r\n");
    printf("=========================================\r\n");

    // 清理串口残留
    HAL_UART_AbortReceive_IT(&huart1);
    while (__HAL_UART_GET_FLAG(&huart1, UART_FLAG_RXNE)) {
        (void)huart1.Instance->DR;
    }
    uint32_t filesize = 0;
    COM_StatusTypeDef err = Ymodem_Receive(&filesize);

    if (err == COM_OK) {
        printf("Firmware received successfully!\r\n");
        // 清除 OTA 标志
        inter_flash_cfg_set_app_update_flag(0);
        printf("OTA flag cleared. Jump to new application...\r\n");
        HAL_Delay(100);
        __disable_irq();
        HAL_SuspendTick();
        Jump_To_App();
    } else {
        printf("Ymodem receive failed, error code: %d\r\n", err);
        HAL_Delay(500);
        NVIC_SystemReset();
    }
    while(1);
}

// ###########################################################################
// ####################### OTA指令检测（收到OTA就升级）########################
// ###########################################################################
void OTA_CheckCommand(uint8_t *data, uint16_t len)
{
    if (len == 0) return;

    // 收到指令：OTA → 进入升级
    if (strstr((char *)data, "OTA") != NULL)
    {
        printf("APP：收到升级指令，准备切换升级模式...\r\n");
				printf("data:%s",data);
				OTA_FLAG = 1;
    }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if(huart->Instance == USART1)
    {
        uint8_t ch = uart_rx_buf[uart_rx_cnt];  

        if(ch == '\r' || ch == '\n')
        {
            if(uart_rx_cnt > 0)
            {
                uart_rx_buf[uart_rx_cnt] = '\0';
								OTA_CheckCommand(uart_rx_buf, uart_rx_cnt);
								
                uart_rx_cnt = 0;                             
            }
        }
        else
        {
            // 增加边界判断，避免数组越界
            if(uart_rx_cnt < RX_BUF_SIZE-1)
            {
                uart_rx_cnt++;  
            }
            // 超出边界则重置计数
            else
            {
                uart_rx_cnt = 0;
            }
        }

        // 重新开启中断时，确保地址不越界
        HAL_UART_Receive_IT(&huart1, &uart_rx_buf[uart_rx_cnt], 1);
    }
}
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
