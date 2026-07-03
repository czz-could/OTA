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

#define CRC16_F /* activate the CRC16 integrity */

/* Define the address from where user application will be loaded.
   Note: this area is reserved for the IAP code                  */
#define FLASH_PAGE_STEP FLASH_PAGE_SIZE          /* Size of page : 2 Kbytes */
#define APPLICATION_ADDRESS (uint32_t)0x08008000 /* Start user code address: ADDR_FLASH_PAGE_8 */

/* Notable Flash addresses */
#define USER_FLASH_END_ADDRESS 0x08080000
/* Define the user application size */
#define USER_FLASH_SIZE ((uint32_t)0x00078000) /* Small default template application */

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
 *     0: end of transmission
 *     2: abort by sender
 *    >0: packet length
 * @param  timeout
 * @retval HAL_OK: normally return
 *         HAL_BUSY: abort by user
 */
static HAL_StatusTypeDef ReceivePacket(uint8_t *p_data, uint32_t *p_length, uint32_t timeout)
{
    uint32_t crc;
    uint32_t packet_size = 0;// 当前包有效载荷长度，默认0
    HAL_StatusTypeDef status;
    uint8_t char1;

    *p_length = 0;
    status = Serial_Recv_data(&char1, 1, timeout);

    if (status == HAL_OK)
    {
        switch (char1)
        {
        case SOH:
            packet_size = PACKET_SIZE;// 载荷128字节
            break;
        case STX:
            packet_size = PACKET_1K_SIZE;// 载荷1024字节
            break;
        case EOT:
            break;// packet_size 保持0，上层识别为传输完毕
        case CA:// 再收第二个字节，必须也是CA
            if ((Serial_Recv_data(&char1, 1, timeout) == HAL_OK) && (char1 == CA))// 标记长度=2，上层判定发送端主动中止
            {
                packet_size = 2;
            }
            else
            {
                status = HAL_ERROR;// 只收到单个CA，判定传输错误
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
            status = Serial_Recv_data(&p_data[PACKET_NUMBER_INDEX], packet_size + PACKET_OVERHEAD_SIZE, timeout);// 事先已接收到SOH或STX，所以3+2-1

            /* Simple packet sanity check */
            if (status == HAL_OK)
            {
                if (p_data[PACKET_NUMBER_INDEX] != ((p_data[PACKET_CNUMBER_INDEX]) ^ NEGATIVE_BYTE))// 进行包号异或校验
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
 * @param  p_data:  output buffer
 * @param  p_file_name: name of the file to be sent
 * @param  length: length of the file to be sent in bytes
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

    /* padding with zeros */
    for (j = i; j < PACKET_SIZE + PACKET_DATA_INDEX; j++)
    {
        p_data[j] = 0;
    }
}

/**
 * @brief  Prepare the data packet
 * @param  p_source: pointer to the data to be sent
 * @param  p_packet: pointer to the output buffer
 * @param  pkt_nr: number of the packet
 * @param  size_blk: length of the block to be sent in bytes
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
 * @param  crc_in input value
 * @param  input byte
 * @retval None
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
 * @param  data
 * @param  length
 * @retval None
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
 * @param  p_data Pointer to input data
 * @param  size length of input data
 * @retval uint8_t checksum value
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

                               发送端                                                         接收端

会话启动                                <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<< C

头文件传输                SOH 00 FF foo.c<0x00>4196<0x20>NULL[117] CRC CRC>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
                                        <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<< ACK
                                        <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<< C

数据传输                  STX 01 FE data[1024] CRC CRC>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
                                        <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<< ACK
                          STX 02 FD data[1024] CRC CRC>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
                                        <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<< ACK
                          STX 03 FC data[1024] CRC CRC>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
                                        <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<< ACK
                          STX 04 FB data[1024] CRC CRC>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
                                        <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<< ACK
                          SOH XX XX data[100] CPMEOF[28] CRC CRC>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
                                        <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<< ACK

文件结束                  EOT>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
                                        <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<< NAK
                          EOT>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
                                        <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<< ACK

会话结束                                <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<< C
                          SOH 00 FF NULL[128] CRC CRC>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
                                        <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<< ACK
																				
																				
发送端:
● SOH (0x01)  128字节包
● STX (0x02)  1024字节包
● EOT (0x04)  传输结束
● foo.c  			文件名
● 0x00  			分隔符
● 0x20  			空格（分隔）
● 发送端要把最后不足整包的剩余字节后面填充 0x00，凑齐完整 128 / 1024 字节发包

接收端
● C (0x43)    发起请求
● ACK (0x06)  确认
● NAK (0x15)  请求重传
● 接收端不收填充的 0，只用前面真实有效长度，Flash 只写入真实文件大小，多余填充字节丢弃
																				



| 数据包开始信号 | 发送序号 | 发送序号反码 | 数据区      | CRC高字节 | CRC低字节 |
| -------------- | -------- | ------------ | ----------- | --------- | --------- |
| SOH/STX        | 01       | FE           | ...         | ...       | ...       |
| 1Byte          | 1Byte    | 1Byte        | 128/1024Byte| 1Byte     | 1Byte     |


数据包开始信号（1 字节）
	SOH：小数据包（头信息、结束会话包，数据长度 128 字节）
	STX：大数据包（文件分片数据，数据长度 1024 字节）
	
发送序号（1 字节）
	帧编号：00、01、02、03、04依次递增，用来校验帧顺序、重传判定,序号走到 255 之后，下一帧直接重置为 0 继续递增循环。
	
发送序号反码（1 字节）
	序号按位取反，示例：序号01 → 反码FE，接收端校验「序号 + 反码」和为0xFF，用来校验序号字节传输是否出错。
	
数据区
	SOH 帧：固定 128 Byte
	STX 帧：固定 1024 Byte
	
CRC 高、低字节（各 1 字节，共 2 字节 CRC 校验）
	对前面全部字段做 CRC 运算，接收端重算 CRC 比对，检测整帧传输误码


*/



/* Public functions ---------------------------------------------------------*/
/**
 * @brief  Receive a file using the ymodem protocol with CRC16.
 * @param  p_size The size of the file.
 * @retval COM_StatusTypeDef result of reception/programming
 */
COM_StatusTypeDef Ymodem_Receive(uint32_t *p_size)
{
    uint32_t other_len = 0;
    uint32_t file_all_num = 0; // 数据包总数
    uint32_t i, packet_length, session_done = 0, file_done, errors = 0, session_begin = 0;
    uint32_t flashdestination, ramsource, filesize;
    uint8_t *file_ptr;
    uint8_t file_size[FILE_SIZE_LENGTH] = {0};
    uint8_t tmp = 0;
    uint32_t packets_received; // 当前接收到包的数量
    // uint32_t last_packets_received; // 当前接收到包的数量    
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
                case 2: // 终止
                    /* Abort by sender */
                    Serial_PutByte(ACK);
                    result = COM_ABORT;
                    break;
                case 0: // 传输完成
                    /* End of transmission 回复 ACK + 自定义 CO*/
                    printf("数据传输完成\r\n");
                    Serial_PutByte(ACK);
                    Serial_PutByte(CRC16);
                    Serial_PutByte(0x4F);

                    file_done = 1;
                    result = COM_OK;

                    session_done = 1;
                    break;
                default:
                    /* Normal packet */
                    // if (aPacketData[PACKET_NUMBER_INDEX] != packets_received)
                    printf("pack:%d ymod_pack%d\r\n", packets_received, aPacketData[PACKET_NUMBER_INDEX]);
                    if ((packets_received % 256) != aPacketData[PACKET_NUMBER_INDEX])// 序号不匹配，回复NAK请求重传本包
                    {
                        printf("---<\r\n");
                        Serial_PutByte(NAK);
                    }
                    else
                    {
                        if (packets_received == 0) // 第一包数据
                        {
                            printf("First pack\r\n");
                            /* File name packet */
                            if (aPacketData[PACKET_DATA_INDEX] != 0) // 文件名不为空
                            {
                                /* File name extraction */
                                i = 0;
                                file_ptr = aPacketData + PACKET_DATA_INDEX;
                                while ((*file_ptr != 0) && (i < FILE_NAME_LENGTH))// 1、解析文件名：读取到0x00分隔符为止
                                {
                                    aFileName[i++] = *file_ptr++;
                                }

                                /* File size extraction */
                                aFileName[i++] = '\0';
                                i = 0;
                                file_ptr++;
                                while ((*file_ptr != ' ') && (i < FILE_SIZE_LENGTH))// 2、跳过0x00分隔符，解析空格前的数字字符串（固件长度）
                                {
                                    file_size[i++] = *file_ptr++;
                                }
                                file_size[i++] = '\0';
                                Str2Int(file_size, &filesize);// ASCII数字串转uint32_t数值长度

                                /* Test the size of the image to be sent */
                                /* Image size is greater than Flash size */
                                if (filesize > (USER_FLASH_SIZE + 1))// 使用内部解析出的真实固件长度判断，不再用*p_size
                                {
                                    /* End session */
                                    tmp = CA;
                                    HAL_UART_Transmit(&huart1, &tmp, 1, NAK_TIMEOUT);
                                    HAL_UART_Transmit(&huart1, &tmp, 1, NAK_TIMEOUT);
                                    result = COM_LIMIT;
                                }
                                /* erase user application area */
																		Erase_Apparea();//擦除app区flash
                                // FLASH_If_Erase(APPLICATION_ADDRESS);

                                *p_size = filesize;
                                other_len = filesize;

                                file_all_num = filesize / PACKET_SIZE;

                                if (file_all_num % PACKET_SIZE != 0)
                                {
                                    file_all_num++;
                                }
                                printf("File Size:%d\r\n", filesize);

                                Serial_PutByte(ACK);
                                Serial_PutByte(CRC16);
                            }
                            /* File header packet is empty, end session */
                            else
                            {
                                printf("File header packet is empty\r\n");
                                Serial_PutByte(ACK);
                                file_done = 1;
                                session_done = 1;
                                break;
                            }
                        }
                        else /* Data packet */
                        {
                            ramsource = (uint32_t)&aPacketData[PACKET_DATA_INDEX];// 可能想开启DMA搬运数据，后面不知道为何方案废弃了

                            memcpy(&flash_buf[flash_buf_rx_cnt], &aPacketData[PACKET_DATA_INDEX], PACKET_SIZE);
                            flash_buf_rx_cnt = flash_buf_rx_cnt + PACKET_SIZE;

                            if (((packets_received % 16) == 0) || (packets_received == file_all_num))// 满2k或者当前接收数据包数量=最大的数据包数量，执行2k写入，然后读出来校验写入过程中有没有出错
                            {
                                flash_buf_rx_cnt = 0;

                                // printf("")
                               // inter_flashif_erase_page(INTER_FLASH_APP_ADDR + (page_cnt * 2048));
                                inter_flashif_write_page(INTER_FLASH_APP_ADDR + (page_cnt * 2048), (uint32_t *)flash_buf, 2048 / 4);

                                HAL_Delay(90);
                                inter_flashif_read_page(INTER_FLASH_APP_ADDR + (page_cnt * 2048), compare_buf, 2040);
                                int ret = memcmp(compare_buf, flash_buf, 2048);

                                printf("cnt:%d addr:%xret:%d\r\n", page_cnt, INTER_FLASH_APP_ADDR + (page_cnt * 2048), ret);
                                page_cnt++;
                                memset(flash_buf, 0, 2048);
                            }

                            // 用于截断末尾上位机补齐的填充 0，不会把多余填充字节写入 Flash，但本次并没有使用这个东西把剩余数据写入，容易出现写入到不存在的地址，出大问题，只能保证升级的
														// app大小小于最大值-2k
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
								case HAL_BUSY: /* Abort actually 发送两次，终止对话*/
                Serial_PutByte(CA);
                Serial_PutByte(CA);
                result = COM_ABORT;
                break;
            default:
                if (session_begin > 0)
                {
                    errors++;
                }
                if (errors > MAX_ERRORS)// 连续5次不成功，结束通信
                {
                    /* Abort communication 发送两次，终止对话**/
                    Serial_PutByte(CA);
                    Serial_PutByte(CA);
                }
                else
                {
                    Serial_PutByte(CRC16); /* Ask for a packet */
                }
                break;
            }
        }
    }
    return result;
}

/**
 * @brief  Transmit a file using the ymodem protocol
 * @param  p_buf: Address of the first byte
 * @param  p_file_name: Name of the file sent
 * @param  file_size: Size of the transmission
 * @retval COM_StatusTypeDef result of the communication
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

        printf("Serial_PutByte(EOT);\r\n");
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
