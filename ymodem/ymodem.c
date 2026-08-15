/*
 * ymodem.c
 *
 *  Created on: 25 July 2023
 *      Author: Akash Virendra
 */

/* Includes ------------------------------------------------------------------*/

#include "ymodem.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#include "stm32f4xx.h"
#include <stdlib.h>
#include <string.h>

QueueHandle_t xUartQueue;

void SendCharacter(uint8_t uc)
{
    while (!(USART1->SR & USART_SR_TXE));
    USART1->DR = uc;
}

typedef enum
{ 
    UART_OK      = 0x00
  , UART_ERROR   = 0x01
  , UART_BUSY    = 0x02
  , UART_TIMEOUT = 0x03
} UART_STATUS;

int UartHandle = 0;
int CrcHandle = 0;

/* Имя файла */
uint8_t aFileName[FILE_NAME_LENGTH];

#define APPLICATION_ADDRESS  0x08000000
#define USER_FLASH_SIZE      (256 * 1024) // 256 KB
#define FLASHIF_OK           0
#define FLASHIF_FAIL         1
#define UART_CLEAR_OREF      0

#define Serial_PutByte(ch)   SendCharacter(ch)

UART_STATUS UART_Receive(int *handle, uint8_t *p_data, uint16_t size, uint32_t timeout_ms)
{
    for (uint16_t i = 0; i < size; i++)
    {
        if (xQueueReceive(xUartQueue, &p_data[i], pdMS_TO_TICKS(timeout_ms)) != pdPASS)
        {
            return UART_TIMEOUT; 
        }
    }
    return UART_OK;
}

UART_STATUS UART_Transmit(int *handle, uint8_t *p_data, uint16_t size, uint32_t timeout_ms)
{
    for (uint16_t i = 0; i < size; i++)
    {
        SendCharacter(p_data[i]);
    }
    return UART_OK;
}

void UART_Delay(uint32_t Delay)
{
    vTaskDelay(pdMS_TO_TICKS(Delay));
}

#define UART_FLUSH_DRREGISTER(handle)  do { volatile uint32_t tmpreg = USART1->SR; tmpreg = USART1->DR; (void)tmpreg; } while(0)
#define UART_CLEAR_IT(handle, flag)    do { volatile uint32_t tmpreg = USART1->SR; tmpreg = USART1->DR; (void)tmpreg; } while(0)

/* Чистая реализация CRC16_Calculate (алгоритм CRC-16 XMODEM) */
uint32_t CRC16_Calculate(int *handle, uint32_t *p_buffer, uint32_t length)
{
    uint8_t *ptr = (uint8_t*)p_buffer;
    uint16_t crc = 0;
    int count = (int)length;
    
    while (--count >= 0) {
        uint32_t tmp = crc ^ (*ptr++ << 8);
        for (int i = 0; i < 8; i++) {
            if (tmp & 0x8000) tmp = (tmp << 1) ^ 0x1021;
            else tmp <<= 1;
        }
        crc = (uint16_t)tmp;
    }
    return crc;
}

/* Вспомогательная функция конвертации числа в ASCII-строку */
void Int2Str(uint8_t *p_str, uint32_t intnum)
{
    uint32_t i, divider = 1000000000, pos = 0, status = 0;
    for (i = 0; i < 10; i++)
    {
        if ((intnum / divider) > 0) status = 1;
        if (status == 1)
        {
            p_str[pos++] = (intnum / divider) + '0';
            intnum = intnum % divider;
        }
        divider /= 10;
    }
    if (pos == 0) p_str[pos++] = '0';
    p_str[pos] = '\0';
}

/* Вспомогательная функция конвертации строки ASCII в число для парсинга размера файла */
uint32_t Str2Int(uint8_t *p_inputstr, uint32_t *p_intnum)
{
    uint32_t i = 0, res = 0;
    if (p_inputstr[0] == '0' && (p_inputstr[1] == 'x' || p_inputstr[1] == 'X')) {
        i = 2;
        while (p_inputstr[i] != '\0') {
            if (p_inputstr[i] >= '0' && p_inputstr[i] <= '9') res = res * 16 + (p_inputstr[i] - '0');
            else if (p_inputstr[i] >= 'A' && p_inputstr[i] <= 'F') res = res * 16 + (p_inputstr[i] - 'A' + 10);
            else if (p_inputstr[i] >= 'a' && p_inputstr[i] <= 'f') res = res * 16 + (p_inputstr[i] - 'a' + 10);
            else return 0;
            i++;
        }
    } else {
        while (p_inputstr[i] != '\0') {
            if (p_inputstr[i] >= '0' && p_inputstr[i] <= '9') res = res * 10 + (p_inputstr[i] - '0');
            else return 0;
            i++;
        }
    }

    *p_intnum = res;

    return 1;
}

#define MAX_FILE_SIZE 61440

typedef struct
{
    uint8_t data[MAX_FILE_SIZE];
    uint32_t length;
} DB_File_t;

extern DB_File_t g_receivedFile;

/* Переменная-счетчик для отслеживания текущей позиции записи в ОЗУ-буфер */
static uint32_t RAM_Write_Ptr = 0;

uint32_t FLASH_If_Erase(uint32_t start) 
{ 
    RAM_Write_Ptr = 0; 
    g_receivedFile.length = 0;
    return FLASHIF_OK; 
}

uint32_t FLASH_If_Write(uint32_t dest, uint32_t *src, uint32_t len) 
{
    uint32_t bytes_to_write = len * 4; 

    if (RAM_Write_Ptr + bytes_to_write > MAX_FILE_SIZE)
    {
        return FLASHIF_FAIL;
    }

    memcpy(&g_receivedFile.data[RAM_Write_Ptr], (uint8_t*)src, bytes_to_write);

    RAM_Write_Ptr += bytes_to_write;

    return FLASHIF_OK; 
}

/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/
#define CRC16_F       /* activate the CRC16 integrity */
/* Private macro -------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
/* @note ATTENTION - please keep this variable 32bit alligned */
uint8_t aPacketData[PACKET_1K_SIZE + PACKET_DATA_INDEX + PACKET_TRAILER_SIZE];

/* Private function prototypes -----------------------------------------------*/
static void PrepareIntialPacket(uint8_t *p_data, const uint8_t *p_file_name, uint32_t length);
static void PreparePacket(uint8_t *p_source, uint8_t *p_packet, uint8_t pkt_nr, uint32_t size_blk);
static UART_STATUS ReceivePacket(uint8_t *p_data, uint32_t *p_length, uint32_t timeout);
uint16_t UpdateCRC16(uint16_t crc_in, uint8_t byte);
uint16_t Cal_CRC16(const uint8_t* p_data, uint32_t size);
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
  * @retval UART_OK: normally return
  *         UART_BUSY: abort by user
  */
static UART_STATUS ReceivePacket(uint8_t *p_data, uint32_t *p_length, uint32_t timeout)
{
  uint32_t crc;
  uint32_t packet_size = 0;
  UART_STATUS status;
  uint8_t char1;

  *p_length = 0;
  status = UART_Receive(&UartHandle, &char1, 1, timeout);

  if (status == UART_OK)
  {
    switch (char1)
    {
      case SOH:
        packet_size = PACKET_SIZE;
        break;
      case STX:
        packet_size = PACKET_1K_SIZE;
        break;
      case EOT:
        break;
      case CA:
        if ((UART_Receive(&UartHandle, &char1, 1, timeout) == UART_OK) && (char1 == CA))
        {
          packet_size = 2;
        }
        else
        {
          status = UART_ERROR;
        }
        break;
      case ABORT1:
      case ABORT2:
        status = UART_BUSY;
        break;
      default:
        status = UART_ERROR;
        break;
    }
    *p_data = char1;

    if (packet_size >= PACKET_SIZE )
    {
      status = UART_Receive(&UartHandle, &p_data[PACKET_NUMBER_INDEX], packet_size + PACKET_OVERHEAD_SIZE, timeout);

      /* Simple packet sanity check */
      if (status == UART_OK )
      {
        if (p_data[PACKET_NUMBER_INDEX] != ((p_data[PACKET_CNUMBER_INDEX]) ^ NEGATIVE_BYTE))
        {
          packet_size = 0;
          status = UART_ERROR;
        }
        else
        {
          /* Check packet CRC */
          crc = p_data[ packet_size + PACKET_DATA_INDEX ] << 8;
          crc += p_data[ packet_size + PACKET_DATA_INDEX + 1 ];
          if (CRC16_Calculate(&CrcHandle, (uint32_t*)&p_data[PACKET_DATA_INDEX], packet_size) != crc )
          {
            packet_size = 0;
            status = UART_ERROR;
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
  Int2Str (astring, length);
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
  for (i = PACKET_DATA_INDEX; i < size + PACKET_DATA_INDEX;i++)
  {
    p_packet[i] = *p_record++;
  }
  if ( size  <= packet_size)
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
    if(in & 0x100)
      ++crc;
    if(crc & 0x10000)
      crc ^= 0x1021;
  }

  while(!(in & 0x10000));

  return crc & 0xffffu;
}

/**
  * @brief  Cal CRC16 for YModem Packet
  * @param  data
  * @param  length
  * @retval None
  */
uint16_t Cal_CRC16(const uint8_t* p_data, uint32_t size)
{
  uint32_t crc = 0;
  const uint8_t* dataEnd = p_data+size;

  while(p_data < dataEnd)
    crc = UpdateCRC16(crc, *p_data++);

  crc = UpdateCRC16(crc, 0);
  crc = UpdateCRC16(crc, 0);

  return crc&0xffffu;
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

  while (p_data < p_data_end )
  {
    sum += *p_data++;
  }

  return (sum & 0xffu);
}

/* Public functions ---------------------------------------------------------*/
/**
  * @brief  Receive a file using the ymodem protocol with CRC16.
  * @param  p_size The size of the file.
  * @retval COM_StatusTypeDef result of reception/programming
  */
COM_StatusTypeDef Ymodem_Receive ( uint32_t *p_size )
{
  uint32_t i, packet_length, session_done = 0, file_done, errors = 0, session_begin = 0;
  uint32_t flashdestination, ramsource, filesize;
  uint8_t *file_ptr;
  uint8_t file_size[FILE_SIZE_LENGTH], tmp, packets_received;
  COM_StatusTypeDef result = COM_OK;

  /* Initialize flashdestination variable */
  flashdestination = APPLICATION_ADDRESS;

  while ((session_done == 0) && (result == COM_OK))
  {
    packets_received = 0;
    file_done = 0;
    while ((file_done == 0) && (result == COM_OK))
    {
      switch (ReceivePacket(aPacketData, &packet_length, DOWNLOAD_TIMEOUT))
      {
        case UART_OK:
          errors = 0;
          switch (packet_length)
          {
            case 2:
              /* Abort by sender */
              Serial_PutByte(ACK);
              result = COM_ABORT;
              break;
            case 0:
              /* End of transmission */
              Serial_PutByte(ACK);
              file_done = 1;
              break;
            default:
              /* Normal packet */
              if (aPacketData[PACKET_NUMBER_INDEX] != packets_received)
              {
                Serial_PutByte(NAK);
              }
              else
              {
                if (packets_received == 0)
                {
                  /* File name packet */
                  if (aPacketData[PACKET_DATA_INDEX] != 0)
                  {
                    /* File name extraction */
                    i = 0;
                    file_ptr = aPacketData + PACKET_DATA_INDEX;
                    while ( (*file_ptr != 0) && (i < FILE_NAME_LENGTH))
                    {
                      aFileName[i++] = *file_ptr++;
                    }

                    /* File size extraction */
                    aFileName[i++] = '\0';
                    i = 0;
                    file_ptr ++;
                    while ( (*file_ptr != ' ') && (i < FILE_SIZE_LENGTH))
                    {
                      file_size[i++] = *file_ptr++;
                    }
                    file_size[i++] = '\0';
                    Str2Int(file_size, &filesize);

                    /* Test the size of the image to be sent */
                    /* Image size is greater than Flash size */
                    if (*p_size > (USER_FLASH_SIZE + 1))
                    {
                      /* End session */
                      tmp = CA;
                      UART_Transmit(&UartHandle, &tmp, 1, NAK_TIMEOUT);
                      UART_Transmit(&UartHandle, &tmp, 1, NAK_TIMEOUT);
                      result = COM_LIMIT;
                    }
                    /* erase user application area */
                    FLASH_If_Erase(APPLICATION_ADDRESS);
                    *p_size = filesize;

                    Serial_PutByte(ACK);
                    Serial_PutByte(CRC16);
                  }
                  /* File header packet is empty, end session */
                  else
                  {
                    Serial_PutByte(ACK);
                    file_done = 1;
                    session_done = 1;
                    break;
                  }
                }
                else /* Data packet */
                {
                  ramsource = (uint32_t) & aPacketData[PACKET_DATA_INDEX];

                  /* Write received data in Flash */
                  if (FLASH_If_Write(flashdestination, (uint32_t*) ramsource, packet_length/4) == FLASHIF_OK)
                  {
                    flashdestination += packet_length;
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
                packets_received ++;
                session_begin = 1;
              }
              break;
          }
          break;
        case UART_BUSY: /* Abort actually */
          Serial_PutByte(CA);
          Serial_PutByte(CA);
          result = COM_ABORT;
          break;
        default:
          if (session_begin > 0)
          {
            errors ++;
          }
          if (errors > MAX_ERRORS)
          {
            /* Abort communication */
            Serial_PutByte(CA);
            Serial_PutByte(CA);
          }
          else
          {
            Serial_PutByte(CRC16); /* Ask for a packet */
            Serial_PutByte(ACK); //BFM
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
COM_StatusTypeDef Ymodem_Transmit (uint8_t *p_buf, const uint8_t *p_file_name, uint32_t file_size)
{
  uint32_t errors = 0, ack_recpt = 0, size = 0, pkt_size;
  uint8_t *p_buf_int;
  COM_StatusTypeDef result = COM_OK;
  uint32_t blk_number = 1;
  uint8_t a_rx_ctrl[2];
  uint8_t i;
#ifdef CRC16_F
  uint32_t temp_crc;
#else /* CRC16_F */
  uint8_t temp_chksum;
#endif /* CRC16_F */

  /* Prepare first block - header */
  PrepareIntialPacket(aPacketData, p_file_name, file_size);

  while (( !ack_recpt ) && ( result == COM_OK ))
  {
    /* Send Packet */
    UART_Transmit(&UartHandle, &aPacketData[PACKET_START_INDEX], PACKET_SIZE + PACKET_HEADER_SIZE, NAK_TIMEOUT);

    /* Send CRC or Check Sum based on CRC16_F */
#ifdef CRC16_F
    temp_crc = CRC16_Calculate(&CrcHandle, (uint32_t*)&aPacketData[PACKET_DATA_INDEX], PACKET_SIZE);
    Serial_PutByte(temp_crc >> 8);
    Serial_PutByte(temp_crc & 0xFF);
#else /* CRC16_F */
    temp_chksum = CalcChecksum (&aPacketData[PACKET_DATA_INDEX], PACKET_SIZE);
    Serial_PutByte(temp_chksum);
#endif /* CRC16_F */

    /* Wait for Ack and 'C' */
    if (UART_Receive(&UartHandle, &a_rx_ctrl[0], 1, NAK_TIMEOUT) == UART_OK)
    {
      if (a_rx_ctrl[0] == ACK)
      {
        ack_recpt = 1;
        UART_Receive(&UartHandle, &a_rx_ctrl[0], 1, NAK_TIMEOUT); //BFM added to wait for the 'C'
      }
      else if (a_rx_ctrl[0] == CA)
      {
        if ((UART_Receive(&UartHandle, &a_rx_ctrl[0], 1, NAK_TIMEOUT) == UART_OK) && (a_rx_ctrl[0] == CA))
        {
          UART_Delay( 2 );
          UART_FLUSH_DRREGISTER(&UartHandle);
          UART_CLEAR_IT(&UartHandle, UART_CLEAR_OREF);
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
  while ((size) && (result == COM_OK ))
  {
    /* Prepare next packet */
    PreparePacket(p_buf_int, aPacketData, blk_number, size);
    ack_recpt = 0;
    a_rx_ctrl[0] = 0;
    errors = 0;

    /* Resend packet if NAK for few times else end of communication */
    while (( !ack_recpt ) && ( result == COM_OK ))
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

      UART_Transmit(&UartHandle, &aPacketData[PACKET_START_INDEX], pkt_size + PACKET_HEADER_SIZE, NAK_TIMEOUT);

      /* Send CRC or Check Sum based on CRC16_F */
#ifdef CRC16_F
      temp_crc = CRC16_Calculate(&CrcHandle, (uint32_t*)&aPacketData[PACKET_DATA_INDEX], pkt_size);
      Serial_PutByte(temp_crc >> 8);
      Serial_PutByte(temp_crc & 0xFF);
#else /* CRC16_F */
      temp_chksum = CalcChecksum (&aPacketData[PACKET_DATA_INDEX], pkt_size);
      Serial_PutByte(temp_chksum);
#endif /* CRC16_F */

      /* Wait for Ack */
      if ((UART_Receive(&UartHandle, &a_rx_ctrl[0], 1, NAK_TIMEOUT) == UART_OK) && (a_rx_ctrl[0] == ACK))
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
  while (( !ack_recpt ) && ( result == COM_OK ))
  {
    Serial_PutByte(EOT);

    /* Wait for Ack */
    if (UART_Receive(&UartHandle, &a_rx_ctrl[0], 1, NAK_TIMEOUT) == UART_OK)
    {
      if (a_rx_ctrl[0] == ACK)
      {
        ack_recpt = 1;
      }
      else if (a_rx_ctrl[0] == CA)
      {
        if ((UART_Receive(&UartHandle, &a_rx_ctrl[0], 1, NAK_TIMEOUT) == UART_OK) && (a_rx_ctrl[0] == CA))
        {
          UART_Delay( 2 );
          UART_FLUSH_DRREGISTER(&UartHandle);
          UART_CLEAR_IT(&UartHandle, UART_CLEAR_OREF);
          result = COM_ABORT;
        }
      }
    }
    else
    {
      errors++;
    }

    if (errors >=  MAX_ERRORS)
    {
      result = COM_ERROR;
    }
  }

  /* Empty packet sent - some terminal emulators need this to close session */
  if ( result == COM_OK )
  {
    /* Preparing an empty packet */
    aPacketData[PACKET_START_INDEX] = SOH;
    aPacketData[PACKET_NUMBER_INDEX] = 0;
    aPacketData[PACKET_CNUMBER_INDEX] = 0xFF;
    for (i = PACKET_DATA_INDEX; i < (PACKET_SIZE + PACKET_DATA_INDEX); i++)
    {
      aPacketData [i] = 0x00;
    }

    /* Send Packet */
    UART_Transmit(&UartHandle, &aPacketData[PACKET_START_INDEX], PACKET_SIZE + PACKET_HEADER_SIZE, NAK_TIMEOUT);

    /* Send CRC or Check Sum based on CRC16_F */
#ifdef CRC16_F
    temp_crc = CRC16_Calculate(&CrcHandle, (uint32_t*)&aPacketData[PACKET_DATA_INDEX], PACKET_SIZE);
    Serial_PutByte(temp_crc >> 8);
    Serial_PutByte(temp_crc & 0xFF);
#else /* CRC16_F */
    temp_chksum = CalcChecksum (&aPacketData[PACKET_DATA_INDEX], PACKET_SIZE);
    Serial_PutByte(temp_chksum);
#endif /* CRC16_F */

    /* Wait for Ack and 'C' */
    if (UART_Receive(&UartHandle, &a_rx_ctrl[0], 1, NAK_TIMEOUT) == UART_OK)
    {
      if (a_rx_ctrl[0] == CA)
      {
          UART_Delay( 2 );
          UART_FLUSH_DRREGISTER(&UartHandle);
          UART_CLEAR_IT(&UartHandle, UART_CLEAR_OREF);
          result = COM_ABORT;
      }
    }
  }

  return result; /* file transmitted successfully */
}


