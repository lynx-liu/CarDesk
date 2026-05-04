# TXRX OTA 升级协议与接口说明

## 1. 系统架构

```
┌─────────────────────────────────────────────────────┐
│                    STM32F103C8T6                     │
│                                                      │
│  ┌──────────────┐  0x08000000   4KB   Bootloader    │
│  │  Bootloader  │  (YMODEM接收 + APP跳转)           │
│  └──────┬───────┘                                    │
│         │ 跳转                                        │
│  ┌──────▼───────┐  0x08002000   56KB  APP           │
│  │    APP       │  (CAN解析 + USART通讯)             │
│  └──────────────┘                                    │
│                                                      │
│  USART1 (PA9/PA10) ←→ ARM芯片  (数据通道 + 升级通道) │
│  USART2 (PA2/PA3)  → 调试串口  (调试日志)            │
└─────────────────────────────────────────────────────┘
```

### Flash分区表

| 区域 | 起始地址 | 大小 | 说明 |
|------|----------|------|------|
| Bootloader | `0x08000000` | 8KB (`0x2000`) | IAP引导程序 |
| APP | `0x08002000` | 56KB (`0xE000`) | 应用程序 |

---

## 2. 升级流程概览

### 2.1 Bootloader启动流程

```
复位
  │
  ├─ 1. 早期初始化 (HSI 8MHz, LED闪烁, USART2调试输出)
  ├─ 2. HAL_Init() + SystemClock_Config() (72MHz)
  ├─ 3. USART1/USART2/GPIO/BKP 初始化
  │
  ├─ 4. 检查BKP_DR1升级标志
  │     │
  │     ├─ 无标志 → 等待3秒YMODEM请求
  │     │           │
  │     │           ├─ 收到SOH/STX → 进入YMODEM接收
  │     │           └─ 超时 → 跳转APP
  │     │                     │
  │     │                     ├─ APP有效 → 运行APP
  │     │                     └─ APP无效 → 等待YMODEM
  │     │
  │     └─ 有标志 → 直接进入YMODEM接收 (清除标志)
  │
  ├─ 5. YMODEM接收固件 → Flash写入 → 完整性校验
  │
  └─ 6. 校验通过 → 跳转新APP
```

### 2.2 触发升级的三种方式

| 方式 | 触发条件 | 说明 |
|------|---------|------|
| **方式A** | APP运行时发送`OTA_UPGRADE`命令 | ARM主动触发，推荐方式 |
| **方式B** | 复位后3秒内发送YMODEM | 调试/生产烧录 |
| **方式C** | APP无效自动等待 | 意外情况恢复 |

---

## 3. 方式A：ARM主动触发升级（推荐）

### 3.1 流程时序

```
ARM                         STM32 (APP)              STM32 (Bootloader)
 │                              │                          │
 │──── "OTA_UPGRADE\r\n" ────→ │                          │
 │                              │                          │
 │←────── "OTA_OK\r\n" ───────│                          │
 │                              │ BKP_DR1=0xA5A5          │
 │                              │ NVIC_SystemReset()       │
 │                              │────────────────────────→ │
 │                              │                          │ 检测BKP标志
 │                              │                          │ 清除BKP标志
 │←─────── 'C' (0x43) ────────────────────────────────── │
 │                              │                          │
 │──── SOH Block0 ─────────────────────────────────────→ │
 │←─── ACK + 'C' ────────────────────────────────────── │
 │                              │                          │
 │──── STX Block1 ─────────────────────────────────────→ │
 │←─── ACK ──────────────────────────────────────────── │
 │──── STX Block2 ─────────────────────────────────────→ │
 │←─── ACK ──────────────────────────────────────────── │
 │      ... (重复直到所有数据块传输完成) ...              │
 │                              │                          │
 │──── EOT ────────────────────────────────────────────→ │
 │←─── ACK + 'C' ────────────────────────────────────── │
 │                              │                          │
 │──── SOH Block0 (空结束块) ─────────────────────────→ │
 │←─── ACK ──────────────────────────────────────────── │
 │                              │                          │
 │                              │                  校验通过
 │←─── ACK (升级成功) ──────────────────────────────── │
 │                              │                  跳转APP
```

### 3.2 OTA命令格式

通过**USART1 (PA9/PA10)** 发送：

```
OTA_UPGRADE\r\n
```

- 波特率：115200
- 数据位：8
- 停止位：1
- 校验：无
- 命令必须以`\r\n`结尾
- 命令不区分前后空白，但关键字`OTA_UPGRADE`必须完整

### 3.3 APP端确认回复

APP收到`OTA_UPGRADE\r\n`后，通过USART1回复：

```
OTA_OK\r\n
```

ARM端收到`OTA_OK\r\n`后表示APP已确认，即将复位进入Bootloader。
等待约1秒后，Bootloader会开始发送`'C'`（0x43），此时ARM可以开始YMODEM传输。

---

## 4. YMODEM协议详细说明

### 4.1 协议参数

| 参数 | 值 | 说明 |
|------|-----|------|
| 模式 | YMODEM-CRC | CRC16校验 |
| 块大小 | 128字节(SOH) / 1024字节(STX) | 推荐使用STX |
| 超时 | 5秒 | 接收超时 |
| 最大重试 | 10次 | 第0块重试 |

### 4.2 控制字符

| 字符 | 值 | 说明 |
|------|-----|------|
| SOH | `0x01` | 128字节数据块头 |
| STX | `0x02` | 1024字节数据块头 |
| EOT | `0x04` | 传输结束 |
| ACK | `0x06` | 确认 |
| NAK | `0x15` | 否定应答 |
| CA | `0x18` | 取消(双字节CA CA) |
| CRC16 | `0x43` | 'C' - 请求CRC模式 |

### 4.3 数据块格式

#### 第0块 (文件信息块)

```
┌──────┬──────────┬──────────────────┬─────────────────┬───────┐
│ SOH  │ Block=00 │ Block补码=FF     │ 128字节数据     │CRC16 │
│ 0x01 │  1字节   │   1字节          │                 │2字节  │
└──────┴──────────┴──────────────────┴─────────────────┴───────┘

数据区格式:
┌─────────────────┬──────┬─────────────────┬──────┬─────────┐
│ 文件名 (string) │ 0x00 │ 文件大小 (ASCII) │ 0x00 │ 填充0x00│
│ "firmware.bin"  │      │ "28672"         │      │         │
└─────────────────┴──────┴─────────────────┴──────┴─────────┘
```

#### 数据块 (Block 1, 2, 3, ...)

```
SOH格式 (128字节):
┌──────┬──────────┬──────────┬──────────────┬───────┐
│ SOH  │ Block=N  │ Block补码│ 128字节数据  │CRC16 │
│ 0x01 │  1字节   │  1字节   │              │2字节  │
└──────┴──────────┴──────────┴──────────────┴───────┘

STX格式 (1024字节):
┌──────┬──────────┬──────────┬──────────────┬───────┐
│ STX  │ Block=N  │ Block补码│ 1024字节数据 │CRC16 │
│ 0x02 │  1字节   │  1字节   │              │2字节  │
└──────┴──────────┴──────────┴──────────────┴───────┘
```

#### 结束块 (空第0块)

```
┌──────┬──────────┬──────────┬──────────────┬───────┐
│ SOH  │ Block=00 │ Block补码│ 128字节0x00  │CRC16 │
│ 0x01 │  1字节   │  =FF     │              │2字节  │
└──────┴──────────┴──────────┴──────────────┴───────┘
```

### 4.4 传输流程

```
1. 接收方发送 'C' (0x43) 请求CRC模式
2. 发送方发送第0块 (文件名+大小)
3. 接收方校验CRC16 → ACK + 'C'
4. 发送方发送数据块1, 2, ...
5. 每块接收方校验CRC16 → ACK
6. 发送方发送EOT
7. 接收方 → ACK + 'C'
8. 发送方发送空第0块
9. 接收方 → ACK
10. 传输完成
```

---

## 5. 固件文件要求

### 5.1 文件格式

| 格式 | 说明 | 推荐 |
|------|------|------|
| `.bin` | 纯二进制，起始地址=0x08002000 | **推荐** |
| `.hex` | Intel HEX格式 | 需转换 |

### 5.2 BIN文件生成 (Keil)

1. 打开Keil工程 → Options for Target → User
2. After Build/Rebuild 勾选 Run #1:
   ```
   fromelf --bin --output=.\Objects\firmware.bin .\Objects\TXRX.axf
   ```
3. 编译后在 `Objects/` 目录生成 `firmware.bin`

### 5.3 固件大小限制

- **最大固件大小**: 56KB (`0xE000` = 57344字节)
- **起始地址**: `0x08002000`
- **APP IROM1设置**: `0x08002000`, `0xE000`

### 5.4 完整性校验

Bootloader接收固件后自动校验：
1. **栈指针校验**: `[0x08002000]` 必须在SRAM范围 (`0x20000000-0x20004FFF`)
2. **复位向量校验**: `[0x08002004]` 必须在Flash范围 (`0x08002000-0x08010000`)
3. **CRC32校验**: 计算整包CRC32，调试串口输出校验值

---

## 6. USART接口定义

### 6.1 USART1 - ARM通讯口 (PA9/PA10)

| 参数 | 值 |
|------|-----|
| 波特率 | 115200 |
| 数据位 | 8 |
| 停止位 | 1 |
| 校验 | 无 |
| 流控 | 无 |
| TX引脚 | PA9 (STM32→ARM) |
| RX引脚 | PA10 (ARM→STM32) |

**APP模式下数据格式** (Text模式):
```
[时间戳][序列号][报文名] 数据值\r\n
```

**APP模式下数据格式** (JSON模式):
```json
{"type":"VIST","seq":1,"ts":1234,"id":"0x0CF00400","name":"ETC2","gear":3}
```

**升级模式下的USART1**:
- 纯YMODEM协议通讯
- 不输出调试信息（调试信息走USART2）

### 6.2 USART2 - 调试串口 (PA2/PA3)

| 参数 | 值 |
|------|-----|
| 波特率 | 115200 |
| 数据位 | 8 |
| 停止位 | 1 |
| 校验 | 无 |
| TX引脚 | PA2 (STM32→PC) |
| RX引脚 | PA3 (未使用) |

**调试输出示例**:
```
[BOOT] Reset! HSI=8MHz
[BOOT] HAL_Init OK
[BOOT] HSE OK
[BOOT] Clock 72MHz OK
[IAP] Bootloader v1.0
[IAP] Checking OTA flag...
[IAP] OTA flag detected, entering upgrade mode
[IAP] Erasing flash...
[IAP] Flash erase OK
[IAP] 12%
[IAP] 25%
...
[IAP] 100%
[IAP] Firmware complete!
[IAP] Firmware CRC32=0xABCD1234
[IAP] Firmware verify OK
[IAP] Upgrade SUCCESS! Jumping to new APP...
[IAP] APP stack=0x20003620 entry=0x08002101
[IAP] Jumping to APP
```

---

## 7. ARM端实现指南

### 7.1 触发升级

```c
/* ARM端: 通过USART1发送OTA命令 */
int trigger_ota_upgrade(void)
{
    /* 1. 发送OTA升级命令 */
    uart1_send_string("OTA_UPGRADE\r\n");
    
    /* 2. 等待APP回复 "OTA_OK\r\n" (超时1秒) */
    char resp[16] = {0};
    uint32_t start = get_tick();
    int idx = 0;
    while ((get_tick() - start) < 1000 && idx < 15) {
        if (uart1_has_data()) {
            resp[idx++] = uart1_receive_byte();
            if (strstr(resp, "OTA_OK")) break;
        }
    }
    
    if (!strstr(resp, "OTA_OK")) {
        return -1;  /* APP未响应, 可能未运行 */
    }
    
    /* 3. 等待Bootloader就绪 (复位+初始化约500ms) */
    delay_ms(1000);
    
    /* 4. 等待接收'C'请求 */
    /* Bootloader会持续发送'C'(0x43), 收到后即可开始YMODEM传输 */
    return 0;
}
```

### 7.2 YMODEM发送 (伪代码)

```c
int ymodem_send_firmware(const uint8_t *firmware, uint32_t size)
{
    /* 1. 等待接收'C' */
    if (uart1_receive_byte(5000) != 'C') return -1;
    
    /* 2. 发送第0块: 文件信息 */
    uint8_t block0[128] = {0};
    strcpy((char *)block0, "firmware.bin");
    sprintf((char *)block0 + strlen("firmware.bin") + 1, "%lu", size);
    send_ymodem_block(SOH, 0, block0, 128);
    
    /* 3. 等待 ACK + 'C' */
    if (uart1_receive_byte(5000) != ACK) return -1;
    if (uart1_receive_byte(5000) != 'C') return -1;
    
    /* 4. 发送数据块 */
    uint8_t block_num = 1;
    uint32_t offset = 0;
    while (offset < size) {
        uint16_t chunk = (size - offset >= 1024) ? 1024 : 128;
        uint8_t header = (chunk == 1024) ? STX : SOH;
        
        send_ymodem_block(header, block_num, firmware + offset, chunk);
        
        if (uart1_receive_byte(5000) != ACK) return -1;
        
        offset += chunk;
        block_num++;
    }
    
    /* 5. 发送EOT */
    uart1_send_byte(EOT);
    if (uart1_receive_byte(5000) != ACK) return -1;
    if (uart1_receive_byte(5000) != 'C') return -1;
    
    /* 6. 发送空结束块 */
    uint8_t empty_block[128] = {0};
    send_ymodem_block(SOH, 0, empty_block, 128);
    if (uart1_receive_byte(5000) != ACK) return -1;
    
    return 0;  /* 成功 */
}
```

### 7.3 完整升级流程

```c
void perform_ota_upgrade(const uint8_t *firmware, uint32_t size)
{
    /* 1. 校验固件大小 */
    if (size == 0 || size > 57344) {  /* 最大56KB */
        return;  /* 固件过大 */
    }
    
    /* 2. 发送OTA升级命令 */
    uart1_send_string("OTA_UPGRADE\r\n");
    delay_ms(1500);  /* 等待复位+Bootloader初始化 */
    
    /* 3. YMODEM传输固件 */
    if (ymodem_send_firmware(firmware, size) != 0) {
        return;  /* 传输失败 */
    }
    
    /* 4. 等待新APP启动 */
    delay_ms(1000);
    
    /* 5. 验证: 等待APP输出就绪信息 */
    /* 可通过USART2调试串口观察 [APP] Entry! 输出 */
}
```

---

## 8. 使用PC串口工具手动升级

### 8.1 使用SecureCRT

1. 连接USART1 (115200, 8N1)
2. 复位STM32
3. 在3秒内: Transfer → Send Ymodem → 选择.bin文件
4. 等待传输完成

### 8.2 使用lrzsz (Linux)

```bash
# 复位STM32后3秒内执行:
sz --ymodem firmware.bin > /dev/ttyUSB0 < /dev/ttyUSB0

# 或使用minicom:
# Ctrl+A → S → ymodem → 选择文件
```

### 8.3 使用Python脚本

```python
import serial
import time

def ota_upgrade(port, firmware_path):
    ser = serial.Serial(port, 115200, timeout=1)
    
    # 方法1: 如果APP正在运行, 先发送OTA命令
    ser.write(b'OTA_UPGRADE\r\n')
    
    # 等待APP回复 "OTA_OK"
    resp = b''
    start = time.time()
    while (time.time() - start) < 2:
        ch = ser.read(1)
        if ch:
            resp += ch
            if b'OTA_OK' in resp:
                print("APP确认升级")
                break
    else:
        print("APP未响应, 尝试直接YMODEM")
    
    # 等待Bootloader就绪
    time.sleep(1.0)
    
    # 等待'C'请求
    while True:
        ch = ser.read(1)
        if ch == b'C':
            break
    
    # 使用ymodem库发送文件
    # pip install ymodem
    from ymodem import YMODEM
    def _getc(size, timeout=5):
        return ser.read(size)
    def _putc(data, timeout=5):
        return ser.write(data)
    
    with open(firmware_path, 'rb') as f:
        firmware = f.read()
    
    ymodem = YMODEM(_getc, _putc)
    status = ymodem.send(firmware, filename='firmware.bin')
    
    ser.close()
    return status

# 使用
ota_upgrade('COM3', 'firmware.bin')
```

---

## 9. BKP寄存器升级标志

| 寄存器 | 地址 | 用途 | 值 |
|--------|------|------|-----|
| BKP_DR1 | `0x40006C04` | 升级标志 | `0xA5A5`=需要升级, `0x0000`=正常启动 |

**注意事项**:
- BKP寄存器由VBAT供电，掉电不丢失
- 升级标志在Bootloader检测后立即清除，防止循环升级
- 如果升级失败，Bootloader不会设置标志，下次复位会尝试跳转旧APP

---

## 10. 错误处理

| 错误 | 串口输出 | 原因 | 处理 |
|------|---------|------|------|
| Invalid APP stack pointer | `[IAP] Invalid APP stack pointer: 0x...` | Flash中无有效APP | 发送YMODEM重试 |
| Invalid firmware size | `[IAP] Invalid firmware size!` | 第0块文件大小=0或超限 | 检查固件文件 |
| Flash erase failed | `[IAP] Flash erase failed!` | Flash擦除失败 | 发送CA取消 |
| Flash write error | `[IAP] Flash write error!` | Flash写入失败 | 发送CA取消 |
| Timeout | `[IAP] Timeout receiving file info` | 10次重试未收到第0块 | 重新发送 |
| CRC校验失败 | USART1收到NAK | 数据块CRC错误 | 发送方重发该块 |
| 固件校验失败 | `[IAP] Firmware verify FAILED!` | 栈指针/复位向量非法 | 重新升级 |

---

## 11. 自动升级实现方案

### 11.1 方案概述

当前已实现自动升级的完整流程：

1. ARM通过USART1发送 `OTA_UPGRADE\r\n`
2. APP收到后设置BKP标志，回复 `OTA_OK\r\n`，然后自动复位
3. Bootloader检测BKP标志，自动进入YMODEM接收模式
4. ARM检测到 `'C'` 请求后发送固件
5. 升级完成后自动跳转新APP

**整个流程对ARM端来说只需两步**：发命令 → 发文件。

### 11.2 ARM端自动升级时序 (推荐)

```
ARM端操作流程:
┌─────────────────────────────────────────────────────────┐
│ 1. 检测到需要升级 (例如: 版本号不匹配 / 收到升级指令)   │
│ 2. 发送 "OTA_UPGRADE\r\n"                               │
│ 3. 等待接收 "OTA_OK\r\n" (超时2秒)                      │
│    ├─ 收到 → 继续                                       │
│    └─ 超时 → STM32可能已在Bootloader, 直接尝试YMODEM    │
│ 4. 等待1秒 (Bootloader初始化)                           │
│ 5. 等待接收 'C' (0x43) (超时5秒)                        │
│    ├─ 收到 → 开始YMODEM传输                             │
│    └─ 超时 → 升级失败                                   │
│ 6. YMODEM传输固件                                       │
│ 7. 传输完成 → 等待1秒 → 新APP启动                       │
│ 8. (可选) 发送版本查询命令, 确认升级成功                 │
└─────────────────────────────────────────────────────────┘
```

### 11.3 自动升级的可靠性保障

| 保障措施 | 说明 |
|---------|------|
| BKP标志防循环 | Bootloader检测到BKP标志后立即清除，即使升级失败也不会循环进入升级 |
| YMODEM CRC16校验 | 每个数据块都有CRC16校验，错误自动重传 |
| 固件完整性校验 | 接收完成后校验栈指针+复位向量+CRC32 |
| 超时保护 | 每个阶段都有超时，避免死锁 |
| 失败可重试 | YMODEM传输失败后Bootloader持续等待重试 |
| APP无效保护 | 新APP校验不通过不会跳转，保留旧APP |

### 11.4 升级状态查询 (建议扩展)

建议在APP中增加版本查询命令，ARM升级后可验证：

```
ARM发送: "VERSION\r\n"
APP回复: "TXRX_V2.0.1\r\n"
```

如果需要此功能，可以在 `OTA_CheckCommand()` 中添加版本查询分支。
