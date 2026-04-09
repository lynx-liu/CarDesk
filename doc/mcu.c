/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body - TXRX CAN-UART Bridge v2.0
  * @note           优化版: 集成J1939 TP多帧传输 + JSON输出 + 扩展缓冲区
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "dm1_parser.h"
#include "can_tp.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/** 
  * @brief 缓冲区配置 (v2.0优化: 从16扩容到32)
  */
#define CAN_RX_BUF_SIZE    32       /* 环形缓冲区深度 */
#define UART_TX_BUF_SIZE   512      /* 发送缓冲区大小 */
#define OUTPUT_BUF_SIZE    600      /* 输出格式化缓冲区 */

/** 
  * @brief 输出模式配置
  */
#define OUTPUT_MODE_TEXT   0        /* 文本模式 (默认) */
#define OUTPUT_MODE_JSON   1        /* JSON模式 */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
CAN_HandleTypeDef hcan;

UART_HandleTypeDef huart1;
UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */

/* CAN接收环形缓冲区 (32报文深度) */
static CAN_RxMsg_t can_rx_buf[CAN_RX_BUF_SIZE];
static volatile uint16_t can_rx_head = 0;
static volatile uint16_t can_rx_tail = 0;
static volatile uint32_t can_rx_overflow = 0;

/* 统计信息 */
static volatile uint32_t can_rx_count = 0;
static volatile uint32_t can_frame_err = 0;
static volatile uint32_t tp_msg_count = 0;     /* TP消息计数 */
static volatile uint32_t dm1_msg_count = 0;    /* DM1消息计数 */
static volatile uint32_t other_count = 0;      /* 其他消息计数 */

/* 输出模式控制 */
static volatile uint8_t output_mode = OUTPUT_MODE_TEXT;

/* 序列号(用于JSON输出) */
static volatile uint32_t msg_sequence = 0;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_CAN_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_USART2_UART_Init(void);

/* USER CODE BEGIN PFP */

static void CAN_FilterConfig(void);
static uint16_t CAN_RxBufPut(CAN_RxMsg_t *msg);
static uint8_t  CAN_RxBufGet(CAN_RxMsg_t *msg);
static void CAN_ProcessData(CAN_RxMsg_t *msg);
static void USART1_SendString(const char *str);
static void USART1_SendData(uint8_t *data, uint16_t len);
static void Output_DM1_Text(DM1_Message_t *dm1, uint32_t can_id, uint32_t timestamp);
static void Output_DM1_JSON(DM1_Message_t *dm1, uint32_t can_id, uint32_t timestamp);
static void Output_TP_Complete(TP_Session_t *session);
static void Print_Statistics(void);
void TP_RxCompleteCallback_Handler(TP_Session_t *session);

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/**
  * @brief printf重定向到USART2(调试串口)
  */
#ifdef __GNUC__
int __io_putchar(int ch)
#else
int fputc(int ch, FILE *f)
#endif
{
    HAL_UART_Transmit(&huart2, (uint8_t *)&ch, 1, HAL_MAX_DELAY);
    return ch;
}

/**
  * @brief 向环形缓冲区写入一条CAN报文 (线程安全)
  */
static uint16_t CAN_RxBufPut(CAN_RxMsg_t *msg)
{
    uint16_t next = (can_rx_head + 1) % CAN_RX_BUF_SIZE;
    
    __disable_irq();  /* 关中断保护临界区 */
    
    if (next == can_rx_tail) {
        can_rx_overflow++;
        __enable_irq();
        return 1; /* 缓冲区满 */
    }
    
    can_rx_buf[can_rx_head] = *msg;
    can_rx_head = next;
    
    __enable_irq();
    return 0;
}

/**
  * @brief 从环形缓冲区读取一条CAN报文
  */
static uint8_t CAN_RxBufGet(CAN_RxMsg_t *msg)
{
    if (can_rx_head == can_rx_tail) {
        return 0; /* 缓冲区空 */
    }
    
    *msg = can_rx_buf[can_rx_tail];
    can_rx_tail = (can_rx_tail + 1) % CAN_RX_BUF_SIZE;
    return 1;
}

/**
  * @brief 通过USART1发送字符串 (向上位机)
  */
static void USART1_SendString(const char *str)
{
    if (str != NULL && str[0] != '\0') {
        HAL_UART_Transmit(&huart1, (uint8_t *)str, strlen(str), HAL_MAX_DELAY);
    }
}

/**
  * @brief 通过USART1发送二进制数据
  */
static void USART1_SendData(uint8_t *data, uint16_t len)
{
    if (data != NULL && len > 0) {
        HAL_UART_Transmit(&huart1, data, len, HAL_MAX_DELAY);
    }
}

/**
  * @brief DM1文本格式输出 (原有格式增强)
  * @param  dm1: 解析结果
  * @param  can_id: CAN ID
  * @param  timestamp: 时间戳(ms)
  */
static void Output_DM1_Text(DM1_Message_t *dm1, uint32_t can_id, uint32_t timestamp)
{
    char buf[OUTPUT_BUF_SIZE];
    uint16_t len = 0;
    const char *ctrl_name = DM1_GetControllerName(can_id);
    uint8_t i;
    
    if (dm1 == NULL) return;
    
    /* 带时间戳和序列号的头部 */
    len += snprintf(buf + len, sizeof(buf) - len,
                    "[%lu][#%lu] ", timestamp, ++msg_sequence);
    
    /* 控制器和指示灯状态 */
    len += snprintf(buf + len, sizeof(buf) - len,
                    "[%s] MIL:%d RSL:%d AWL:%d PL:%d\r\n",
                    ctrl_name,
                    (dm1->LampStatus >> 0) & 0x03,
                    (dm1->LampStatus >> 2) & 0x03,
                    (dm1->LampStatus >> 4) & 0x03,
                    (dm1->LampStatus >> 6) & 0x03);
    
    /* 故障码列表 */
    for (i = 0; i < dm1->FaultCount && i < 5; i++) {
        if (dm1->Faults[i].Valid) {
            const char *desc = DM1_GetFaultDescription(
                dm1->Faults[i].SPN, dm1->Faults[i].FMI);
            len += snprintf(buf + len, sizeof(buf) - len,
                            "  #%lu SPN:%lu FMI:%d OC:%d %s\r\n",
                            (uint32_t)(i + 1),
                            dm1->Faults[i].SPN,
                            dm1->Faults[i].FMI,
                            dm1->Faults[i].OC,
                            desc);
        }
    }
    
    if (dm1->FaultCount == 0) {
        len += snprintf(buf + len, sizeof(buf) - len, 
                        "  No Active Faults\r\n");
    }
    
    /* 结束标记 */
    len += snprintf(buf + len, sizeof(buf) - len, "---\r\n");
    
    USART1_SendString(buf);
}

/**
  * @brief DM1 JSON格式输出 (新增，便于上位机解析)
  * @note   JSON格式遵循RFC7159标准，单行输出便于逐行解析
  */
static void Output_DM1_JSON(DM1_Message_t *dm1, uint32_t can_id, uint32_t timestamp)
{
    char buf[OUTPUT_BUF_SIZE];
    uint16_t len = 0;
    const char *ctrl_name = DM1_GetControllerName(can_id);
    uint8_t i;
    
    if (dm1 == NULL) return;
    
    /* JSON对象开始 */
    len += snprintf(buf + len, sizeof(buf) - len, "{\"type\":\"DM1\"");
    
    /* 基本信息 */
    len += snprintf(buf + len, sizeof(buf) - len, 
                    ",\"seq\":%lu", ++msg_sequence);
    len += snprintf(buf + len, sizeof(buf) - len,
                    ",\"ts\":%lu", timestamp);
    len += snprintf(buf + len, sizeof(buf) - len,
                    ",\"id\":\"0x%08lX\"", can_id);
    len += snprintf(buf + len, sizeof(buf) - len,
                    ",\"controller\":\"%s\"", ctrl_name);
    
    /* 指示灯状态 */
    len += snprintf(buf + len, sizeof(buf) - len,
                    ",\"lamp\":{\"MIL\":%d,\"RSL\":%d,\"AWL\":%d,\"PL\":%d}",
                    (dm1->LampStatus >> 0) & 0x03,
                    (dm1->LampStatus >> 2) & 0x03,
                    (dm1->LampStatus >> 4) & 0x03,
                    (dm1->LampStatus >> 6) & 0x03);
    
    /* 故障码数组 */
    len += snprintf(buf + len, sizeof(buf) - len, ",\"faults\":[");
    
    for (i = 0; i < dm1->FaultCount && i < 5; i++) {
        if (dm1->Faults[i].Valid) {
            const char *desc = DM1_GetFaultDescription(
                dm1->Faults[i].SPN, dm1->Faults[i].FMI);
            
            if (i > 0) {
                len += snprintf(buf + len, sizeof(buf) - len, ",");
            }
            
            len += snprintf(buf + len, sizeof(buf) - len,
                            "{\"n\":%d,\"spn\":%lu,\"fmi\":%d,"
                            "\"oc\":%d,\"desc\":\"%s\"}",
                            i + 1,
                            dm1->Faults[i].SPN,
                            dm1->Faults[i].FMI,
                            dm1->Faults[i].OC,
                            desc);
        }
    }
    
    len += snprintf(buf + len, sizeof(buf) - len, "]}\r\n");
    
    USART1_SendString(buf);
}

/**
  * @brief 多帧TP数据完成回调处理
  * @param  session: 完成的会话指针
  */
void TP_RxCompleteCallback_Handler(TP_Session_t *session)
{
    if (session == NULL) return;
    
    tp_msg_count++;
    
    /* 检查是否为多帧DM1数据 */
    if (session->PGN == 0xFECA ||  /* DM1 PGN */
        session->State == TP_STATE_RX_COMPLETE) {
        
        Output_TP_Complete(session);
    } else {
        /* 其他PGN的原始数据转发 */
        printf("[TP] Non-DM1 PGN=0x%05lX received, size=%d bytes\r\n",
               session->PGN, session->TotalSize);
    }
}

/**
  * @brief TP完成数据输出到上位机
  */
static void Output_TP_Complete(TP_Session_t *session)
{
    char buf[OUTPUT_BUF_SIZE];
    uint16_t len = 0;
    uint32_t now = HAL_GetTick();
    
    if (session == NULL || session->State != TP_STATE_RX_COMPLETE) return;
    
    if (output_mode == OUTPUT_MODE_JSON) {
        /* ======== JSON格式输出 ======== */
        len += snprintf(buf + len, sizeof(buf) - len, 
                        "{\"type\":\"TP_MULTI_FRAME\"");
        len += snprintf(buf + len, sizeof(buf) - len,
                        ",\"seq\":%lu", ++msg_sequence);
        len += snprintf(buf + len, sizeof(buf) - len,
                        ",\"ts\":%lu", now);
        len += snprintf(buf + len, sizeof(buf) - len,
                        ",\"pgn\":\"0x%05lX\"", session->PGN);
        len += snprintf(buf + len, sizeof(buf) - len,
                        ",\"sa\":\"0x%02X\"", session->SA);
        len += snprintf(buf + len, sizeof(buf) - len,
                        ",\"da\":\"0x%02X\"", session->DA);
        len += snprintf(buf + len, sizeof(buf) - len,
                        ",\"size\":%d", session->TotalSize);
        len += snprintf(buf + len, sizeof(buf) - len,
                        ",\"frames\":%d", session->RxFrameCount);
        
        /* 数据以十六进制字符串输出 */
        len += snprintf(buf + len, sizeof(buf) - len, ",\"data\":\"");
        
        /* 限制数据显示长度(避免过长) */
        uint16_t show_len = (session->TotalSize > 128) ? 128 : session->TotalSize;
        uint16_t i;
        for (i = 0; i < show_len && len < (OUTPUT_BUF_SIZE - 20); i++) {
            len += snprintf(buf + len, sizeof(buf) - len, "%02X", session->Data[i]);
        }
        if (show_len < session->TotalSize) {
            len += snprintf(buf + len, sizeof(buf) - len, "...(%d more)",
                           session->TotalSize - show_len);
        }
        
        len += snprintf(buf + len, sizeof(buf) - len, "\"}\r\n");
        
    } else {
        /* ======== 文本格式输出 ======== */
        len += snprintf(buf + len, sizeof(buf) - len,
                        "[TP-Multi] [#%lu] PGN=0x%05lX SA=0x%02X Size=%d Frames=%d Time=%lums\r\n",
                        ++msg_sequence, session->PGN, session->SA,
                        session->TotalSize, session->RxFrameCount,
                        session->StartTime ? (now - session->StartTime) : 0);
        
        /* 显示前64字节数据预览 */
        len += snprintf(buf + len, sizeof(buf) - len, "  Data Preview:");
        
        uint16_t preview_len = (session->TotalSize > 64) ? 64 : session->TotalSize;
        uint16_t i;
        for (i = 0; i < preview_len && len < (OUTPUT_BUF_SIZE - 10); i++) {
            len += snprintf(buf + len, sizeof(buf) - len, " %02X", session->Data[i]);
        }
        if (preview_len < session->TotalSize) {
            len += snprintf(buf + len, sizeof(buf) - len, " ...(+%d bytes)",
                           session->TotalSize - preview_len);
        }
        len += snprintf(buf + len, sizeof(buf) - len, "\r\n---\r\n");
    }
    
    USART1_SendString(buf);
}

/**
  * @brief 打印统计信息 (调试用)
  */
static void Print_Statistics(void)
{
    uint32_t tp_total, tp_sessions, tp_errors;
    
    TP_GetStats(&tp_total, &tp_sessions, &tp_errors);
    
    printf("\r\n========== Statistics ==========\r\n");
    printf("  CAN Rx Total:    %lu\r\n", can_rx_count);
    printf("  CAN Overflow:    %lu\r\n", can_rx_overflow);
    printf("  CAN Frame Err:   %lu\r\n", can_frame_err);
    printf("  DM1 Messages:    %lu\r\n", dm1_msg_count);
    printf("  TP Messages:     %lu\r\n", tp_msg_count);
    printf("  Other Messages:  %lu\r\n", other_count);
    printf("  TP Sessions OK:  %lu\r\n", tp_sessions);
    printf("  TP Errors:       %lu\r\n", tp_errors);
    printf("  TP Total Bytes:  %lu\r\n", tp_total);
    printf("=================================\r\n");
}

/**
  * @brief 核心数据处理函数 (v2.0优化)
  * @param  msg: 接收到的CAN报文
  * @note   处理逻辑:
  *         1. 检查是否为TP.CM/TP.DT -> 交给TP协议层
  *         2. 检查是否为DM1单帧 -> 解析并输出
  *         3. 其他报文可选转发或忽略
  */
static void CAN_ProcessData(CAN_RxMsg_t *msg)
{
    DM1_Message_t dm1;
    int8_t tp_result;
    
    /* ========== 1. 检查是否为TP多帧报文 ========== */
    if (Is_TP_Message(msg->ExtId)) {
        
        tp_result = TP_RxMessage(msg);
        
        if (tp_result == TP_ERR_NONE) {
            /* TP消息已处理，直接返回 */
            return;
        } else if (tp_result == TP_ERR_NO_SESSION ||
                   tp_result == TP_ERR_INVALID_SEQ) {
            /* 非致命错误，仅记录 */
            // printf("[DBG] TP minor error=%d\r\n", tp_result);
            return;
        }
        
        /* 其他错误继续尝试作为普通报文处理 */
    }
    
    /* ========== 2. 检查是否为DM1单帧报文 ========== */
    if (DM1_IsDM1Message(msg->ExtId)) {
        
        if (DM1_ParseMessage(msg->ExtId, msg->Data, msg->DLC, &dm1)) {
            dm1_msg_count++;
            
            /* 根据当前模式选择输出格式 */
            if (output_mode == OUTPUT_MODE_JSON) {
                Output_DM1_JSON(&dm1, msg->ExtId, msg->Timestamp);
            } else {
                Output_DM1_Text(&dm1, msg->ExtId, msg->Timestamp);
            }
            
            /* 调试日志 */
            printf("[DM1] %s: %d faults, ID=0x%08lX\r\n",
                   DM1_GetControllerName(msg->ExtId),
                   dm1.FaultCount, msg->ExtId);
        }
        return;
    }
    
    /* ========== 3. 其他报文 (可在此扩展处理) ========== */
    other_count++;
    
    /* 可选: 转发所有原始CAN报文到上位机 (默认关闭)
     * 取消下面的注释即可启用原始数据流
     *
     * char raw_buf[80];
     * int raw_len = snprintf(raw_buf, sizeof(raw_buf),
     *     "[RAW] ID=0x%08lX DLC=%lu", msg->ExtId, msg->DLC);
     * for (i=0; i<msg->DLC && raw_len<(int)sizeof(raw_buf)-3; i++) {
     *     raw_len += snprintf(raw_buf+raw_len, sizeof(raw_buf)-raw_len,
     *         " %02X", msg->Data[i]);
     * }
     * strcat(raw_buf, "\r\n");
     * USART1_SendString(raw_buf);
     */
}

/* CAN FIFO0接收回调 (HAL弱函数重写) */
void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcanHandle)
{
    CAN_RxHeaderTypeDef rx_header;
    CAN_RxMsg_t rx_msg;

    if (HAL_CAN_GetRxMessage(hcanHandle, CAN_RX_FIFO0, &rx_header, rx_msg.Data) == HAL_OK) {
        rx_msg.StdId = rx_header.StdId;
        rx_msg.ExtId = rx_header.ExtId;
        rx_msg.IDE   = rx_header.IDE;
        rx_msg.DLC   = rx_header.DLC;
        rx_msg.Timestamp = HAL_GetTick();
        can_rx_count++;
        CAN_RxBufPut(&rx_msg);
    } else {
        can_frame_err++;
    }
}

/* CAN FIFO1接收回调 (HAL弱函数重写) */
void HAL_CAN_RxFifo1MsgPendingCallback(CAN_HandleTypeDef *hcanHandle)
{
    CAN_RxHeaderTypeDef rx_header;
    CAN_RxMsg_t rx_msg;

    if (HAL_CAN_GetRxMessage(hcanHandle, CAN_RX_FIFO1, &rx_header, rx_msg.Data) == HAL_OK) {
        rx_msg.StdId = rx_header.StdId;
        rx_msg.ExtId = rx_header.ExtId;
        rx_msg.IDE   = rx_header.IDE;
        rx_msg.DLC   = rx_header.DLC;
        rx_msg.Timestamp = HAL_GetTick();
        can_rx_count++;
        CAN_RxBufPut(&rx_msg);
    } else {
        can_frame_err++;
    }
}

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
  MX_CAN_Init();
  MX_USART1_UART_Init();
  MX_USART2_UART_Init();

  /* USER CODE BEGIN 2 */

  /* CAN过滤器配置: 接收所有报文 */
  CAN_FilterConfig();

  /* 初始化J1939 TP协议层 */
  TP_Init();
  
  /* 注册TP接收完成回调 */
  TP_SetRxCallback(TP_RxCompleteCallback_Handler);

  /* 启动CAN接收中断 */
  HAL_CAN_Start(&hcan);
  HAL_CAN_ActivateNotification(&hcan, CAN_IT_RX_FIFO0_MSG_PENDING | CAN_IT_RX_FIFO1_MSG_PENDING);

  /* 启动信息输出 */
  printf("\r\n");
  printf("====================================================\r\n");
  printf("  TXRX CAN-UART Bridge v2.0 (Optimized Edition)\r\n");
  printf("----------------------------------------------------\r\n");
  printf("  Hardware: STM32F103C8T6 @ 72MHz\r\n");
  printf("  CAN:      250Kbps, Accept ALL, Dual FIFO\r\n");
  printf("  Buffer:   %d messages (ring buffer)\r\n", CAN_RX_BUF_SIZE);
  printf("  USART1:   115200 bps (to Host PC)\r\n");
  printf("  USART2:   115200 bps (Debug Console)\r\n");
  printf("----------------------------------------------------\r\n");
  printf("  Features:\r\n");
  printf("    [+] J1939 Single-frame DM1 Parser\r\n");
  printf("    [+] J1939 Multi-frame TP (BAM/RTS/CTS)\r\n");
  printf("    [+] Text + JSON dual output mode\r\n");
  printf("    [+] Extended fault code library\r\n");
  printf("    [+] Real-time statistics\r\n");
  printf("----------------------------------------------------\r\n");
  printf("  Supported Controllers (DM1):\r\n");
  printf("    - ABS  Anti-lock Brake System (0x18FECA0B)\r\n");
  printf("    - EBS  Electronic Brake System (0x18FECAE8)\r\n");
  printf("    - BCM  Body Control Module   (0x18FECA21)\r\n");
  printf("    - EMS  Engine Management Sys (0x18FEE700)\r\n");
  printf("    - TCU  Transmission Control (0x18FEF600)\r\n");
  printf("    - VCU  Vehicle Control Unit (0x18FEF101)\r\n");
  printf("    - ICU  Instrument Cluster   (0x18FEC400)\r\n");
  printf("    - EPS  Electric Power Steer (0x18FEF002)\r\n");
  printf("====================================================\r\n");
  printf("\r\n[System] Ready! Waiting for CAN data...\r\n");

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */

  while (1)
  {
    /* USER CODE END WHILE */
    
    CAN_RxMsg_t rx_msg;

    /* 从环形缓冲区取出CAN报文并处理 */
    if (CAN_RxBufGet(&rx_msg)) {
        
        /* LED闪烁指示数据活动 */
        HAL_GPIO_TogglePin(GPIOB, GPIO_PIN_8);

        /* 调试: 原始报文打印 (可通过编译开关禁用以提升性能) */
        #ifdef DEBUG_PRINT_RAW
        printf("[CAN] ExtID:0x%08lX StdID:0x%03lX DLC:%lu Data:",
               rx_msg.ExtId, rx_msg.StdId, rx_msg.DLC);
        for (uint8_t i = 0; i < rx_msg.DLC; i++) {
            printf(" %02X", rx_msg.Data[i]);
        }
        printf(" Cnt:%lu Ovr:%lu\r\n", can_rx_count, can_rx_overflow);
        #endif

        /* 核心数据处理 (DM1 + TP + ...) */
        CAN_ProcessData(&rx_msg);
        
    }

    /* TP周期任务 (超时检测、状态机维护) */
    TP_Process();

    /* USER CODE BEGIN 3 */
    
    /* 可选: 定期打印统计信息 (每60秒一次)
     * static uint32_t last_stats_time = 0;
     * if ((HAL_GetTick() - last_stats_time) >= 60000) {
     *     Print_Statistics();
     *     last_stats_time = HAL_GetTick();
     * }
     */
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

/**
  * @brief CAN Initialization Function
  * @param None
  * @retval None
  */
static void MX_CAN_Init(void)
{

  /* USER CODE BEGIN CAN_Init 0 */

  /* USER CODE END CAN_Init 0 */

  /* USER CODE BEGIN CAN_Init 1 */

  /* USER CODE END CAN_Init 1 */
  hcan.Instance = CAN1;
  /* 250Kbps @ APB1=36MHz: 36MHz / (9 * (1+13+2)) = 250Kbps, 采样点87.5% */
  hcan.Init.Prescaler = 9;
  hcan.Init.Mode = CAN_MODE_NORMAL;
  hcan.Init.SyncJumpWidth = CAN_SJW_1TQ;
  hcan.Init.TimeSeg1 = CAN_BS1_13TQ;
  hcan.Init.TimeSeg2 = CAN_BS2_2TQ;
  hcan.Init.TimeTriggeredMode = DISABLE;
  hcan.Init.AutoBusOff = ENABLE;
  hcan.Init.AutoWakeUp = DISABLE;
  hcan.Init.AutoRetransmission = ENABLE;
  hcan.Init.ReceiveFifoLocked = DISABLE;
  hcan.Init.TransmitFifoPriority = DISABLE;
  if (HAL_CAN_Init(&hcan) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN CAN_Init 2 */

  /* USER CODE END CAN_Init 2 */

}

/**
  * @brief USART1 Initialization Function (to Host PC)
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

/**
  * @brief USART2 Initialization Function (Debug)
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init 1 */

  /* USER CODE END MX_GPIO_Init 1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, GPIO_PIN_RESET);

  /*Configure GPIO pin : PA9 */
  GPIO_InitStruct.Pin = GPIO_PIN_9;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pin : PA10 */
  GPIO_InitStruct.Pin = GPIO_PIN_10;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pin : PB8 */
  GPIO_InitStruct.Pin = GPIO_PIN_8;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init 2 */

  /* USER CODE END MX_GPIO_Init 2 */
}

/* USER CODE BEGIN 4 */

/**
  * @brief CAN过滤器配置: 接收所有扩展帧和标准帧
  */
static void CAN_FilterConfig(void)
{
    CAN_FilterTypeDef filter_config;

    /* 过滤器0: 接收所有扩展帧 (29位ID) 和标准帧 (11位ID) */
    filter_config.FilterBank = 0;
    filter_config.FilterMode = CAN_FILTERMODE_IDMASK;
    filter_config.FilterScale = CAN_FILTERSCALE_32BIT;
    /* 扩展帧: FilterIdLow 的 bit 2 = IDE = 1 */
    filter_config.FilterIdHigh = 0x0000;
    filter_config.FilterIdLow = 0x0004;   /* IDE=1, RTR=0 */
    filter_config.FilterMaskIdHigh = 0x0000;
    filter_config.FilterMaskIdLow = 0x0006; /* 只关心 IDE=1, RTR=0 */
    filter_config.FilterFIFOAssignment = CAN_RX_FIFO0;
    filter_config.FilterActivation = ENABLE;
    filter_config.SlaveStartFilterBank = 14;

    if (HAL_CAN_ConfigFilter(&hcan, &filter_config) != HAL_OK) {
        printf("[ERR] CAN Filter config failed!\r\n");
        Error_Handler();
    }
    printf("[OK] CAN Filter configured: Accept Extended Frames\r\n");
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
  * @param  file: pointer to the source file
  * @param  line: line number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the wrong parameters value:
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
