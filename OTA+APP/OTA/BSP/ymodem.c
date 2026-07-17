#include "ymodem.h"
/**
 ******************************************************************************
 * @file    IAP_Main/Src/ymodem.c
 * @author  MCD Application Team
 * @version V1.6.0
 * @date    12-May-2017
 * @brief   This file provides all the software functions related to the ymodem
 *          protocol.
 ******************************************************************************
 * @attention
 *
 * <h2><center>&copy; Copyright ? 2016 STMicroelectronics International N.V.
 * All rights reserved.</center></h2>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted, provided that the following conditions are met:
 *
 * 1. Redistribution of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. Neither the name of STMicroelectronics nor the names of other
 *    contributors to this software may be used to endorse or promote products
 *    derived from this software without specific written permission.
 * 4. This software, including modifications and/or derivative works of this
 *    software, must execute solely and exclusively on microcontroller or
 *    microprocessor devices manufactured by or for STMicroelectronics.
 * 5. Redistribution and use of this software other than as permitted under
 *    this license is void and will automatically terminate your rights under
 *    this license.
 *
 * THIS SOFTWARE IS PROVIDED BY STMICROELECTRONICS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS, IMPLIED OR STATUTORY WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
 * PARTICULAR PURPOSE AND NON-INFRINGEMENT OF THIRD PARTY INTELLECTUAL PROPERTY
 * RIGHTS ARE DISCLAIMED TO THE FULLEST EXTENT PERMITTED BY LAW. IN NO EVENT
 * SHALL STMICROELECTRONICS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 ******************************************************************************
 */

/** @addtogroup STM32F1xx_IAP
 * @{
 */

/* Includes ------------------------------------------------------------------*/
// #include "flash_if.h"
#include "common.h"
#include "ymodem.h"
#include "string.h"
#include "main.h"
#include "ymodem_porting.h"
#include "usart.h"
#include "inter_flashif.h"

#define CRC16_F /* 开启CRC16数据校验 */

/* Define the address from where user application will be loaded.
   Note: this area is reserved for the IAP code                  */
#define FLASH_PAGE_STEP FLASH_PAGE_SIZE          /* Flash页大小：2K字节 */
#define APPLICATION_ADDRESS (uint32_t)0x08008000 /* 用户程序起始地址：第8页Flash */

/* Notable Flash addresses */
#define USER_FLASH_END_ADDRESS 0x08080000
/* Define the user application size */
#define USER_FLASH_SIZE ((uint32_t)0x00078000) /* 默认用户应用最大空间 */

/* @note ATTENTION - please keep this variable 32bit alligned */
uint8_t aPacketData[PACKET_1K_SIZE + PACKET_DATA_INDEX + PACKET_TRAILER_SIZE];

uint8_t aFileName[FILE_NAME_LENGTH];

uint8_t flash_buf[2048] = {0};
uint8_t compare_buf[2048] = {0};
uint32_t flash_buf_rx_cnt = 0;
uint32_t page_cnt = 0;
/* Private function prototypes -----------------------------------------------*/
static void PrepareIntialPacket(uint8_t *p_data, const uint8_t *p_file_name, uint32_t length);
static void PreparePacket(uint8_t *p_source, uint8_t *p_packet, uint8_t pkt_nr, uint32_t size_blk);
static HAL_StatusTypeDef ReceivePacket(uint8_t *p_data, uint32_t *p_length, uint32_t timeout);
uint16_t UpdateCRC16(uint16_t crc_in, uint8_t byte);
uint16_t Cal_CRC16(const uint8_t *p_data, uint32_t size);
uint8_t CalcChecksum(const uint8_t *p_data, uint32_t size);

/* Private functions ---------------------------------------------------------*/

/**
 * @brief  Receive a packet from sender
 * @param  data
 * @param  length
 *     0: 传输结束
 *     2: 发送方中止传输
 *    >0: 数据包有效长度
 * @param  timeout 超时时间
 * @retval HAL_OK: 正常接收
 *         HAL_BUSY: 用户主动中止
 */
static HAL_StatusTypeDef ReceivePacket(uint8_t *p_data, uint32_t *p_length, uint32_t timeout)
{
    uint32_t crc;
    uint32_t packet_size = 0;// 当前数据包有效长度，初始为0
    HAL_StatusTypeDef status;
    uint8_t char1;

    *p_length = 0;
    status = Serial_Recv_data(&char1, 1, timeout);

    if (status == HAL_OK)
    {
        switch (char1)
        {
        case SOH:
            packet_size = PACKET_SIZE;// 小包 128字节
            break;
        case STX:
            packet_size = PACKET_1K_SIZE;// 大包 1024字节
            break;
        case EOT:
            break;// packet_size保持0，代表传输结束标识
        case CA:// 连续两个CA代表传输中止
            if ((Serial_Recv_data(&char1, 1, timeout) == HAL_OK) && (char1 == CA))// 收到连续两个CA，判定发送方停止传输
            {
                packet_size = 2;
            }
            else
            {
                status = HAL_ERROR;// 只收到单个CA，判定为错误帧
            }
            break;
        case ABORT1:
        case ABORT2:
            status = HAL_BUSY;
            break;
        default:
            status = HAL_ERROR;
            break;
        }
        *p_data = char1;

        if (packet_size >= PACKET_SIZE)
        {
            status = Serial_Recv_data(&p_data[PACKET_NUMBER_INDEX], packet_size + PACKET_OVERHEAD_SIZE, timeout);// 读取SOH/STX后剩余数据

            /* Simple packet sanity check */
            if (status == HAL_OK)
            {
                if (p_data[PACKET_NUMBER_INDEX] != ((p_data[PACKET_CNUMBER_INDEX]) ^ NEGATIVE_BYTE))// 校验帧号与反码是否匹配
                {
                    packet_size = 0;
                    status = HAL_ERROR;
                }
                else
                {
                    /* Check packet CRC */
                    crc = p_data[packet_size + PACKET_DATA_INDEX] << 8;
                    crc += p_data[packet_size + PACKET_DATA_INDEX + 1];
                    if (Cal_CRC16(&p_data[PACKET_DATA_INDEX], packet_size) != crc)
                    {
                        packet_size = 0;
                        status = HAL_ERROR;
                    }
                }
            }
            else
            {
                packet_size = 0;
            }
        }
    }
    *p_length = packet_size;
    return status;
}

/**
 * @brief  Prepare the first block
 * @param  p_data: 输出缓存
 * @param  p_file_name: 待传输文件名
 * @param  length: 文件总字节长度
 * @retval None
 */
static void PrepareIntialPacket(uint8_t *p_data, const uint8_t *p_file_name, uint32_t length)
{
    uint32_t i, j = 0;
    uint8_t astring[10];

    /* first 3 bytes are constant */
    p_data[PACKET_START_INDEX] = SOH;
    p_data[PACKET_NUMBER_INDEX] = 0x00;
    p_data[PACKET_CNUMBER_INDEX] = 0xff;

    /* Filename written */
    for (i = 0; (p_file_name[i] != '\0') && (i < FILE_NAME_LENGTH); i++)
    {
        p_data[i + PACKET_DATA_INDEX] = p_file_name[i];
    }

    p_data[i + PACKET_DATA_INDEX] = 0x00;

    /* file size written */
    Int2Str(astring, length);
    i = i + PACKET_DATA_INDEX + 1;
    while (astring[j] != '\0')
    {
        p_data[i++] = astring[j++];
    }
    p_data[i++] = ' ';  // 使用空格符进行分割

    /* padding with zeros */
    for (j = i; j < PACKET_SIZE + PACKET_DATA_INDEX; j++)
    {
        p_data[j] = 0;
    }
}

/**
 * @brief  Prepare the data packet
 * @param  p_source: 待发送数据指针
 * @param  p_packet: 输出数据包缓存
 * @param  pkt_nr: 帧序号
 * @param  size_blk: 当前块有效字节长度
 * @retval None
 */
static void PreparePacket(uint8_t *p_source, uint8_t *p_packet, uint8_t pkt_nr, uint32_t size_blk)
{
    uint8_t *p_record;
    uint32_t i, size, packet_size;

    /* Make first three packet */
    packet_size = size_blk >= PACKET_1K_SIZE ? PACKET_1K_SIZE : PACKET_SIZE;
    size = size_blk < packet_size ? size_blk : packet_size;
    if (packet_size == PACKET_1K_SIZE)
    {
        p_packet[PACKET_START_INDEX] = STX;
    }
    else
    {
        p_packet[PACKET_START_INDEX] = SOH;
    }
    p_packet[PACKET_NUMBER_INDEX] = pkt_nr;
    p_packet[PACKET_CNUMBER_INDEX] = (~pkt_nr);
    p_record = p_source;

    /* Filename packet has valid data */
    for (i = PACKET_DATA_INDEX; i < size + PACKET_DATA_INDEX; i++)
    {
        p_packet[i] = *p_record++;
    }
    if (size <= packet_size)
    {
        for (i = size + PACKET_DATA_INDEX; i < packet_size + PACKET_DATA_INDEX; i++)
        {
            p_packet[i] = 0x1A; /* EOF (0x1A) or 0x00 */
        }
    }
}

/**
 * @brief  Update CRC16 for input byte
 * @param  crc_in 输入CRC值
 * @param  byte 待计算字节
 * @retval 更新后的CRC
 */
uint16_t UpdateCRC16(uint16_t crc_in, uint8_t byte)
{
    uint32_t crc = crc_in;
    uint32_t in = byte | 0x100;

    do
    {
        crc <<= 1;
        in <<= 1;
        if (in & 0x100)
            ++crc;
        if (crc & 0x10000)
            crc ^= 0x1021;
    }

    while (!(in & 0x10000));

    return crc & 0xffffu;
}

/**
 * @brief  Cal CRC16 for YModem Packet
 * @param  data 数据缓冲区
 * @param  size 数据长度
 * @retval CRC16结果
 */
uint16_t Cal_CRC16(const uint8_t *p_data, uint32_t size)
{
    uint32_t crc = 0;
    const uint8_t *dataEnd = p_data + size;

    while (p_data < dataEnd)
        crc = UpdateCRC16(crc, *p_data++);

    crc = UpdateCRC16(crc, 0);
    crc = UpdateCRC16(crc, 0);

    return crc & 0xffffu;
}

/**
 * @brief  Calculate Check sum for YModem Packet
 * @param  p_data 输入数据指针
 * @param  size 数据长度
 * @retval uint8_t 校验和结果
 */
uint8_t CalcChecksum(const uint8_t *p_data, uint32_t size)
{
    uint32_t sum = 0;
    const uint8_t *p_data_end = p_data + size;

    while (p_data < p_data_end)
    {
        sum += *p_data++;
    }

    return (sum & 0xffu);
}



/*

                                发送方                                                  接收方

文件头帧                <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<< C
SOH 00 FF foo.c<0x00>4196<0x20>NULL[117] CRC CRC>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
                        <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<< ACK
                        <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<< C

数据帧                  STX 01 FE data[1024] CRC CRC>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
                        <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<< ACK
                        STX 02 FD data[1024] CRC CRC>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
                        <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<< ACK
                        STX 03 FC data[1024] CRC CRC>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
                        <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<< ACK
                        STX 04 FB data[1024] CRC CRC>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
                        <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<< ACK
                        SOH XX XX data[100] CPMEOF[28] CRC CRC>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
                        <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<< ACK

结束帧                  EOT>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
                        <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<< NAK
                        EOT>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
                        <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<< ACK

会话结束空帧            <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<< C
                        SOH 00 FF NULL[128] CRC CRC>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
                        <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<< ACK

发送方标识：
SOH (0x01)  128字节小包
STX (0x02)  1024字节大包
EOT (0x04)  文件传输结束
foo.c       文件名
0x00        文件名结束符
0x20        空格分隔符
不足128/1024字节的数据段末尾填充0x00补齐帧长度

接收方标识：
C (0x43)    请求CRC校验模式
ACK (0x06)  正确接收应答
NAK (0x15)  接收错误，请求重传
缓存2KB数据后一次性写入Flash，仅写入有效数据长度，末尾不足2KB部分只写入实际有效字节

| 帧起始标识 | 帧序号 | 帧序号反码 | 数据域        | CRC高字节 | CRC低字节 |
| -------------- | -------- | ------------ | ----------- | --------- | --------- |
| SOH/STX        | 01       | FE           | ...         | ...       | ...       |
| 1Byte          | 1Byte    | 1Byte        | 128/1024Byte| 1Byte     | 1Byte     |

帧起始标识 1字节：
	SOH 小包帧头，文件头帧默认使用，数据段长度128字节
	STX 大包帧头，大数据分片传输，数据段长度1024字节

帧序号 1字节：
	从0、1、2、3依次递增，用于校验帧顺序，序号超过255自动循环归零

帧序号反码 1字节：
	与帧序号相加等于0xFF，用于校验帧是否传输错乱

数据域：
	SOH帧固定128 Byte
	STX帧固定1024 Byte

CRC校验域 2字节：
	对前面全部数据计算CRC16，接收端对比校验值判断数据是否出错
*/



/* Public functions ---------------------------------------------------------*/
/**
 * @brief  使用Ymodem CRC16模式接收固件文件
 * @param  p_size 输出文件总字节大小
 * @retval COM_StatusTypeDef 接收/烧录结果状态
 */
COM_StatusTypeDef Ymodem_Receive(uint32_t *p_size)
{
    uint32_t other_len = 0;
    uint32_t file_all_num = 0; // 总分片数量
    uint32_t i, packet_length, session_done = 0, file_done, errors = 0, session_begin = 0;
    uint32_t flashdestination, ramsource, filesize;
    uint8_t *file_ptr;
    uint8_t file_size[FILE_SIZE_LENGTH] = {0};
    uint8_t tmp = 0;
    uint32_t packets_received; // 当前收到的帧序号
    // uint32_t last_packets_received; // 上一帧序号
    COM_StatusTypeDef result = COM_OK;

    /* Initialize flashdestination variable */
    flashdestination = APPLICATION_ADDRESS;

    while ((session_done == 0) && (result == COM_OK))
    {
        packets_received = 0;
        // last_packets_received = 0;
        file_done = 0;
        file_all_num = 0;
        flash_buf_rx_cnt = 0;
        page_cnt = 0;
        while ((file_done == 0) && (result == COM_OK))
        {
            switch (ReceivePacket(aPacketData, &packet_length, DOWNLOAD_TIMEOUT))
            {
            case HAL_OK:
                errors = 0;
                switch (packet_length)
                {
                case 2: // 传输中止
                    /* Abort by sender */
                    Serial_PutByte(ACK);
                    result = COM_ABORT;
                    break;
                case 0: // 文件传输结束
                    /* End of transmission 返回ACK + 结束标识CO*/
                    printf("文件传输完成\r\n");
                    Serial_PutByte(ACK);
                    Serial_PutByte(CRC16);
                    Serial_PutByte(0x4F);

                    file_done = 1;
                    result = COM_OK;

                    session_done = 1;
                    break;
                default:
                    /* Normal packet 正常数据帧 */
                    // if (aPacketData[PACKET_NUMBER_INDEX] != packets_received)
                    printf("本地帧计数:%d 接收帧序号:%d\r\n", packets_received, aPacketData[PACKET_NUMBER_INDEX]);
                    if ((packets_received % 256) != aPacketData[PACKET_NUMBER_INDEX])// 帧序号不匹配，回复NAK请求重传
                    {
                        printf("帧序号错误\r\n");
                        Serial_PutByte(NAK);
                    }
                    else
                    {
                        if (packets_received == 0) // 第一帧文件头帧
                        {
                            printf("收到文件头帧\r\n");
                            /* File name packet */
                            if (aPacketData[PACKET_DATA_INDEX] != 0) // 文件名字段非空
                            {
                                /* File name extraction */
                                i = 0;
                                file_ptr = aPacketData + PACKET_DATA_INDEX;
                                while ((*file_ptr != 0) && (i < FILE_NAME_LENGTH))// 读取文件名直到0x00结束符
                                {
                                    aFileName[i++] = *file_ptr++;
                                }

                                /* File size extraction */
                                aFileName[i++] = '\0';
                                i = 0;
                                file_ptr++;
                                while ((*file_ptr != ' ') && (i < FILE_SIZE_LENGTH))// 跳过文件名结束符，读取空格前的文件大小字符串
                                {
                                    file_size[i++] = *file_ptr++;
                                }
                                file_size[i++] = '\0';
                                Str2Int(file_size, &filesize);// ASCII字符串转uint32_t数字

                                /* Test the size of the image to be sent */
                                /* Image size is greater than Flash size */
                                if (filesize > (USER_FLASH_SIZE + 1))// 校验文件大小，超出Flash分区则报错
                                {
                                    /* End session */
                                    tmp = CA;
                                    HAL_UART_Transmit(&huart1, &tmp, 1, NAK_TIMEOUT);
                                    HAL_UART_Transmit(&huart1, &tmp, 1, NAK_TIMEOUT);
                                    result = COM_LIMIT;
                                }
                                /* erase user application area */
                                Erase_Apparea();//擦除App对应Flash区域
                                // FLASH_If_Erase(APPLICATION_ADDRESS);

                                *p_size = filesize;
                                other_len = filesize;

                                file_all_num = filesize / PACKET_SIZE;

                                if (file_all_num % PACKET_SIZE != 0)
                                {
                                    file_all_num++;
                                }
                                printf("文件总大小:%d字节\r\n", filesize);

                                Serial_PutByte(ACK);
                                Serial_PutByte(CRC16);
                            }
                            /* File header packet is empty, end session */
                            else
                            {
                                printf("空文件头帧，结束传输会话\r\n");
                                Serial_PutByte(ACK);
                                file_done = 1;
                                session_done = 1;
                                break;
                            }
                        }
                        else /* Data packet 业务数据帧 */
                        {
                            ramsource = (uint32_t)&aPacketData[PACKET_DATA_INDEX];// 数据起始地址

                            memcpy(&flash_buf[flash_buf_rx_cnt], &aPacketData[PACKET_DATA_INDEX], PACKET_SIZE);
                            flash_buf_rx_cnt = flash_buf_rx_cnt + PACKET_SIZE;

                            if (((packets_received % 16) == 0) || (packets_received == file_all_num))// 攒够2KB数据或最后一帧，执行Flash页写入，写入后校验
                            {
                                flash_buf_rx_cnt = 0;

                                // inter_flashif_erase_page(INTER_FLASH_APP_ADDR + (page_cnt * 2048));
                                inter_flashif_write_page(INTER_FLASH_APP_ADDR + (page_cnt * 2048), (uint32_t *)flash_buf, 2048 / 4);

                                HAL_Delay(90);
                                inter_flashif_read_page(INTER_FLASH_APP_ADDR + (page_cnt * 2048), compare_buf, 2040);
                                int ret = memcmp(compare_buf, flash_buf, 2048);

                                printf("当前页:%d 写入地址:%x 校验结果:%d\r\n", page_cnt, INTER_FLASH_APP_ADDR + (page_cnt * 2048), ret);
                                page_cnt++;
                                memset(flash_buf, 0, 2048);
                            }

                            // 文件末尾不足2KB时，剩余数据暂存缓存，全部接收完成后再写入Flash，防止多余填充数据污染固件
							// App最小存储单位2KB
                            if (other_len >= packet_length)
                            {
                                other_len = other_len - packet_length;
                            }

                            if (1)
                            {
                                Serial_PutByte(ACK);
                            }

                            else /* An error occurred while writing to Flash memory */
                            {
                                /* End session */
                                Serial_PutByte(CA);
                                Serial_PutByte(CA);
                                result = COM_DATA;
                            }
                        }
                        packets_received++;
                        session_begin = 1;
                    }
                    break;
                }
                break;
            case HAL_BUSY: /* Abort actually 收到中止指令，终止传输会话*/
                Serial_PutByte(CA);
                Serial_PutByte(CA);
                result = COM_ABORT;
                break;
            default:
                if (session_begin > 0)
                {
                    errors++;
                }
                if (errors > MAX_ERRORS)// 错误次数超过最大重试次数，断开通信
                {
                    /* Abort communication 发送双CA中止传输**/
                    Serial_PutByte(CA);
                    Serial_PutByte(CA);
                }
                else
                {
                    Serial_PutByte(CRC16); /* 发送C，请求重发当前数据包 */
                }
                break;
            }
        }
    }
    return result;
}

/**
 * @brief  Transmit a file using the ymodem protocol
 * @param  p_buf: 固件数据起始地址
 * @param  p_file_name: 待发送文件名
 * @param  file_size: 文件总字节长度
 * @retval COM_StatusTypeDef 通信结果状态
 */
COM_StatusTypeDef Ymodem_Transmit(uint8_t *p_buf, const uint8_t *p_file_name, uint32_t file_size)
{
    uint32_t errors = 0, ack_recpt = 0, size = 0, pkt_size;
    uint8_t *p_buf_int;
    COM_StatusTypeDef result = COM_OK;
    uint32_t blk_number = 1;
    uint8_t a_rx_ctrl[2];
    uint8_t i;
#ifdef CRC16_F
    uint32_t temp_crc;
#else  /* CRC16_F */
    uint8_t temp_chksum;
#endif /* CRC16_F */

    /* Prepare first block - header */
    PrepareIntialPacket(aPacketData, p_file_name, file_size);

    while ((!ack_recpt) && (result == COM_OK))
    {
        /* Send Packet */
        HAL_UART_Transmit(&huart1, &aPacketData[PACKET_START_INDEX], PACKET_SIZE + PACKET_HEADER_SIZE, NAK_TIMEOUT);

        /* Send CRC or Check Sum based on CRC16_F */
#ifdef CRC16_F
        temp_crc = Cal_CRC16(&aPacketData[PACKET_DATA_INDEX], PACKET_SIZE);
        Serial_PutByte(temp_crc >> 8);
        Serial_PutByte(temp_crc & 0xFF);
#else  /* CRC16_F */
        temp_chksum = CalcChecksum(&aPacketData[PACKET_DATA_INDEX], PACKET_SIZE);
        Serial_PutByte(temp_chksum);
#endif /* CRC16_F */

        /* Wait for Ack and 'C' */
        if (Serial_Recv_data(&a_rx_ctrl[0], 1, NAK_TIMEOUT) == HAL_OK)
        {
            if (a_rx_ctrl[0] == ACK)
            {
                ack_recpt = 1;
            }
            else if (a_rx_ctrl[0] == CA)
            {
                if ((Serial_Recv_data(&a_rx_ctrl[0], 1, NAK_TIMEOUT) == HAL_OK) && (a_rx_ctrl[0] == CA))
                {
                    HAL_Delay(2);
                    __HAL_UART_FLUSH_DRREGISTER(&huart1);
                    result = COM_ABORT;
                }
            }
        }
        else
        {
            errors++;
        }
        if (errors >= MAX_ERRORS)
        {
            result = COM_ERROR;
        }
    }

    p_buf_int = p_buf;
    size = file_size;

    /* Here 1024 bytes length is used to send the packets */
    while ((size) && (result == COM_OK))
    {
        /* Prepare next packet */
        PreparePacket(p_buf_int, aPacketData, blk_number, size);
        ack_recpt = 0;
        a_rx_ctrl[0] = 0;
        errors = 0;

        /* Resend packet if NAK for few times else end of communication */
        while ((!ack_recpt) && (result == COM_OK))
        {
            /* Send next packet */
            if (size >= PACKET_1K_SIZE)
            {
                pkt_size = PACKET_1K_SIZE;
            }
            else
            {
                pkt_size = PACKET_SIZE;
            }

            HAL_UART_Transmit(&huart1, &aPacketData[PACKET_START_INDEX], pkt_size + PACKET_HEADER_SIZE, NAK_TIMEOUT);

            /* Send CRC or Check Sum based on CRC16_F */
#ifdef CRC16_F
            temp_crc = Cal_CRC16(&aPacketData[PACKET_DATA_INDEX], pkt_size);
            Serial_PutByte(temp_crc >> 8);
            Serial_PutByte(temp_crc & 0xFF);
#else  /* CRC16_F */
            temp_chksum = CalcChecksum(&aPacketData[PACKET_DATA_INDEX], pkt_size);
            Serial_PutByte(temp_chksum);
#endif /* CRC16_F */

            /* Wait for Ack */
            if ((Serial_Recv_data(&a_rx_ctrl[0], 1, NAK_TIMEOUT) == HAL_OK) && (a_rx_ctrl[0] == ACK))
            {
                ack_recpt = 1;
                if (size > pkt_size)
                {
                    p_buf_int += pkt_size;
                    size -= pkt_size;
                    if (blk_number == (USER_FLASH_SIZE / PACKET_1K_SIZE))
                    {
                        result = COM_LIMIT; /* boundary error */
                    }
                    else
                    {
                        blk_number++;
                    }
                }
                else
                {
                    p_buf_int += pkt_size;
                    size = 0;
                }
            }
            else
            {
                errors++;
            }

            /* Resend packet if NAK  for a count of 10 else end of communication */
            if (errors >= MAX_ERRORS)
            {
                result = COM_ERROR;
            }
        }
    }

    /* Sending End Of Transmission char */
    ack_recpt = 0;
    a_rx_ctrl[0] = 0x00;
    errors = 0;
    while ((!ack_recpt) && (result == COM_OK))
    {

        printf("发送EOT结束帧\r\n");
        Serial_PutByte(EOT);

        /* Wait for Ack */
        if (Serial_Recv_data(&a_rx_ctrl[0], 1, NAK_TIMEOUT) == HAL_OK)
        {
            if (a_rx_ctrl[0] == ACK)
            {
                ack_recpt = 1;
            }
            else if (a_rx_ctrl[0] == CA)
            {
                if ((Serial_Recv_data(&a_rx_ctrl[0], 1, NAK_TIMEOUT) == HAL_OK) && (a_rx_ctrl[0] == CA))
                {
                    HAL_Delay(2);
                    __HAL_UART_FLUSH_DRREGISTER(&huart1);
                    result = COM_ABORT;
                }
            }
        }
        else
        {
            errors++;
        }

        if (errors >= MAX_ERRORS)
        {
            result = COM_ERROR;
        }
    }

    /* Empty packet sent - some terminal emulators need this to close session */
    if (result == COM_OK)
    {
        /* Preparing an empty packet */
        aPacketData[PACKET_START_INDEX] = SOH;
        aPacketData[PACKET_NUMBER_INDEX] = 0;
        aPacketData[PACKET_CNUMBER_INDEX] = 0xFF;
        for (i = PACKET_DATA_INDEX; i < (PACKET_SIZE + PACKET_DATA_INDEX); i++)
        {
            aPacketData[i] = 0x00;
        }

        /* Send Packet */
        HAL_UART_Transmit(&huart1, &aPacketData[PACKET_START_INDEX], PACKET_SIZE + PACKET_HEADER_SIZE, NAK_TIMEOUT);

        /* Send CRC or Check Sum based on CRC16_F */
#ifdef CRC16_F
        temp_crc = Cal_CRC16(&aPacketData[PACKET_DATA_INDEX], PACKET_SIZE);
        Serial_PutByte(temp_crc >> 8);
        Serial_PutByte(temp_crc & 0xFF);
#else  /* CRC16_F */
        temp_chksum = CalcChecksum(&aPacketData[PACKET_DATA_INDEX], PACKET_SIZE);
        Serial_PutByte(temp_chksum);
#endif /* CRC16_F */

        /* Wait for Ack and 'C' */
        if (Serial_Recv_data(&a_rx_ctrl[0], 1, NAK_TIMEOUT) == HAL_OK)
        {
            if (a_rx_ctrl[0] == CA)
            {
                HAL_Delay(2);
                __HAL_UART_FLUSH_DRREGISTER(&huart1);
                result = COM_ABORT;
            }
        }
    }

    return result; /* file transmitted successfully */
}

/**
 * @}
 */

/*******************(C)COPYRIGHT 2016 STMicroelectronics *****END OF FILE****/