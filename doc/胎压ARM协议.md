# MCU→ARM 胎压显示通信协议

> **用途**：ARM主程序依据本协议解析MCU推送的胎压数据，并在车机屏幕上显示  
> **版本**：V1.0  
> **日期**：2026-09-14  
> **依据**：SQ250 Product Message protocol + TC04-M主接收器规格书 + TXRX程序实现

---

## 1. 物理层参数

| 参数 | 值 |
|------|-----|
| 接口 | USART1 (PA9=TX, PA10=RX) |
| 波特率 | 115200 bps |
| 数据格式 | 8数据位, 1停止位, 无校验, 无流控 (8N1) |
| 发送方式 | DMA非阻塞发送 + 1024字节环形缓冲区 |
| 通信方向 | 双向（MCU→ARM推送数据，ARM→MCU发送命令） |

---

## 2. 输出模式选择

MCU支持两种输出模式，**强烈建议ARM端使用JSON模式**（结构化，便于解析显示）。

| 模式 | 切换命令 | 说明 |
|------|----------|------|
| 文本模式(默认) | `OUTPUT_TEXT\r\n` | 人类可读, 调试用 |
| **JSON模式(推荐)** | `OUTPUT_JSON\r\n` | 结构化, 便于解析 |

**切换方式**：通过USART1向MCU发送对应命令字符串即可。

---

## 3. TPMS数据帧定义

### 3.1 JSON格式（推荐ARM使用）

每收到一条SQ250 CAN报文(CAN ID `0x18FEF433`)，MCU立即通过USART1推送一帧JSON：

```
{"type":"TPMS","seq":42,"ts":12345,"id":"0x18FEF433","axle":0,"tire":0,"pressure_kpa":752,"temperature_c":35.00,"leakage_pa_s":0.0,"alarm":"NORMAL"}\r\n
```

**字段说明**：

| 字段 | 类型 | 单位 | 说明 |
|------|------|------|------|
| type | string | — | 固定"TPMS" |
| seq | uint32 | — | 帧序号, 每帧+1 |
| ts | uint32 | ms | MCU上电毫秒数, 用于判断数据新鲜度 |
| id | string | — | 固定"0x18FEF433" |
| **axle** | uint8 | — | **车桥编号: 0=前桥, 1=后桥, ..., 6=备胎** |
| **tire** | uint8 | — | **轮胎编号: 0~3, 从左到右** |
| **pressure_kpa** | uint16 | kPa | **轮胎压力(显示用)** |
| **temperature_c** | float | ℃ | **轮胎温度(显示用)** |
| leakage_pa_s | float | Pa/s | 漏气速率(可选显示) |
| **alarm** | string | — | **报警等级(决定显示颜色)** |

### 3.2 文本格式（备用调试）

```
[12345][#42][TPMS] Axle0 Tire0 | P=752kPa T=35.0C Leak=0.0Pa/s Alarm=NORMAL
```

正则解析示例：`\[TPMS\] Axle(\d+) Tire(\d+) \| P=(\d+)kPa T=([0-9.]+)C.*Alarm=(\w+)`

---

## 4. 显示逻辑（核心）

### 4.1 axle/tire → 屏幕位置映射

| axle | tire | 轮胎位置 | 屏幕显示区域 |
|------|------|----------|-------------|
| 0 | 0 | 前桥左外 | 左前 |
| 0 | 1 | 前桥右外 | 右前 |
| 0 | 2 | 前桥左内(6轮车) | 左前内 |
| 0 | 3 | 前桥右内(6轮车) | 右前内 |
| 1 | 0 | 后桥左外 | 左后 |
| 1 | 1 | 后桥右外 | 右后 |
| 1 | 2 | 后桥左内 | 左后内 |
| 1 | 3 | 后桥右内 | 右后内 |
| 2 | 0 | 第三桥左外 | 左中 |
| 2 | 1 | 第三桥右外 | 右中 |
| ... | ... | ... | ... |
| 6 | 0 | 备胎 | 备胎区 |

### 4.2 报警等级 → 显示颜色/图标

| alarm值 | 颜色 | 图标 | 说明 |
|---------|------|------|------|
| NORMAL | 绿色 | ✓ | 正常, 压力697.5~1209kPa |
| LOW | 黄色 | ⚠ | 低压报警, 压力465~697.5kPa |
| HIGH | 黄色 | ⚠ | 高压报警, 压力1209~1395kPa |
| ULTRA_LOW | 红色 | ✗ | 超低压, <465kPa, 危险! |
| ULTRA_HIGH | 红色 | ✗ | 超高压, >1395kPa, 危险! |

### 4.3 压力显示单位转换

ARM端可按需转换显示单位：

| 显示单位 | 转换公式 | 示例 |
|----------|----------|------|
| kPa | 直接显示 | 752 kPa |
| bar | kPa ÷ 100 | 7.52 bar |
| PSI | kPa ÷ 6.895 | 109.1 PSI |
| kg/cm² | kPa ÷ 98.066 | 7.67 kg/cm² |

---

## 5. 显示UI布局建议

### 5.1 标准卡车布局（4轮）

```
┌─────────────────────────────────────┐
│          胎压监测系统               │
├─────────────────────────────────────┤
│                                     │
│   左前          右前               │
│  ┌────┐        ┌────┐              │
│  │752 │        │680 │              │
│  │kPa │        │kPa │              │
│  │35℃│        │36℃│              │
│  │ ✓  │        │ ⚠  │              │
│  └────┘        └────┘              │
│   绿色          黄色               │
│                                     │
│   左后          右后               │
│  ┌────┐        ┌────┐              │
│  │1400│        │464 │              │
│  │kPa │        │kPa │              │
│  │40℃│        │35℃│              │
│  │ ✗  │        │ ✗  │              │
│  └────┘        └────┘              │
│   红色          红色               │
│                                     │
├─────────────────────────────────────┤
│ 备胎: 无数据                        │
└─────────────────────────────────────┘
```

### 5.2 6轮卡车布局

```
┌─────────────────────────────────────┐
│  左前外/内   右前外/内              │
│  ┌──┐┌──┐    ┌──┐┌──┐              │
│  │752││850│   │680││730│            │
│  └──┘└──┘    └──┘└──┘              │
│                                     │
│  左后外/内   右后外/内              │
│  ┌──┐┌──┐    ┌──┐┌──┐              │
│  │..││..│    │..││..│              │
│  └──┘└──┘    └──┘└──┘              │
└─────────────────────────────────────┘
```

---

## 6. 数据新鲜度判断

MCU的 `ts` 字段是上电毫秒数。ARM端判断数据是否过期：

```python
current_ts = mcu_uptime_ms  # 需要从MCU获取或本地维护
if (current_ts - tire.ts) > 5000:
    # 超过5秒未更新, 显示"无信号"
    show_no_signal(axle, tire)
elif (current_ts - tire.ts) > 2000:
    # 超过2秒, 显示"信号弱" + 灰色
    show_weak_signal(axle, tire)
```

**建议阈值**：
- < 1000ms: 正常显示
- 1000~3000ms: 数据可能过期, 灰色显示
- > 3000ms: 无信号, 显示"--"

---

## 7. ARM端解析代码示例

### 7.1 Python示例

```python
import json, serial

# 轮胎位置映射表
TIRE_LAYOUT = {
    (0, 0): "左前",  (0, 1): "右前",
    (0, 2): "左前内", (0, 3): "右前内",
    (1, 0): "左后",  (1, 1): "右后",
    (1, 2): "左后内", (1, 3): "右后内",
    (6, 0): "备胎",
}

# 报警颜色映射
ALARM_COLOR = {
    "NORMAL":     ("#00FF00", "✓"),  # 绿色
    "LOW":        ("#FFFF00", "⚠"),  # 黄色
    "HIGH":       ("#FFFF00", "⚠"),  # 黄色
    "ULTRA_LOW":  ("#FF0000", "✗"),  # 红色
    "ULTRA_HIGH": ("#FF0000", "✗"),  # 红色
}

# 轮胎数据缓存
tire_data = {}  # key=(axle,tire), value=dict

ser = serial.Serial('COM4', 115200, timeout=1)

def parse_tpms(line):
    """解析一行JSON数据"""
    try:
        data = json.loads(line)
        if data.get('type') != 'TPMS':
            return None
        key = (data['axle'], data['tire'])
        tire_data[key] = {
            'pressure': data['pressure_kpa'],
            'temp': data['temperature_c'],
            'leak': data['leakage_pa_s'],
            'alarm': data['alarm'],
            'ts': data['ts'],
            'seq': data['seq'],
        }
        return data
    except json.JSONDecodeError:
        return None

def update_display():
    """更新车机屏幕显示"""
    for (axle, tire), data in tire_data.items():
        pos = TIRE_LAYOUT.get((axle, tire), f"桥{axle}轮{tire}")
        color, icon = ALARM_COLOR.get(data['alarm'], ("#FFFFFF", "?"))
        # 更新UI控件
        print(f"{pos}: {data['pressure']}kPa {data['temp']}℃ {data['alarm']} [{icon}]")

while True:
    line = ser.readline().decode('utf-8').strip()
    if line:
        result = parse_tpms(line)
        if result:
            update_display()
```

### 7.2 C/C++示例（嵌入式ARM）

```c
#include <string.h>
#include <stdlib.h>

typedef struct {
    uint8_t  axle;
    uint8_t  tire;
    uint16_t pressure_kpa;
    float    temperature_c;
    float    leakage_pa_s;
    char     alarm[16];
    uint32_t ts;
    uint8_t  valid;
} TireDisplay_t;

static TireDisplay_t tire_display[7][4];  /* [axle][tire] */

/* 简易JSON解析 */
int parse_tpms_json(const char *line) {
    const char *p;
    TireDisplay_t t = {0};
    
    if (strstr(line, "\"type\":\"TPMS\"") == NULL) return -1;
    
    /* axle */
    p = strstr(line, "\"axle\":");
    if (p) sscanf(p, "\"axle\":%hhu", &t.axle);
    
    /* tire */
    p = strstr(line, "\"tire\":");
    if (p) sscanf(p, "\"tire\":%hhu", &t.tire);
    
    /* pressure */
    p = strstr(line, "\"pressure_kpa\":");
    if (p) sscanf(p, "\"pressure_kpa\":%hu", &t.pressure_kpa);
    
    /* temperature */
    p = strstr(line, "\"temperature_c\":");
    if (p) sscanf(p, "\"temperature_c\":%f", &t.temperature_c);
    
    /* leakage */
    p = strstr(line, "\"leakage_pa_s\":");
    if (p) sscanf(p, "\"leakage_pa_s\":%f", &t.leakage_pa_s);
    
    /* alarm */
    p = strstr(line, "\"alarm\":\"");
    if (p) sscanf(p, "\"alarm\":\"%15[^\"]\"", t.alarm);
    
    /* ts */
    p = strstr(line, "\"ts\":");
    if (p) sscanf(p, "\"ts\":%lu", &t.ts);
    
    t.valid = 1;
    if (t.axle < 7 && t.tire < 4) {
        memcpy(&tire_display[t.axle][t.tire], &t, sizeof(t));
    }
    return 0;
}

/* 显示颜色获取 */
uint32_t get_alarm_color(const char *alarm) {
    if (strcmp(alarm, "NORMAL") == 0)      return 0x00FF00;  /* 绿 */
    if (strcmp(alarm, "LOW") == 0)         return 0xFFFF00;  /* 黄 */
    if (strcmp(alarm, "HIGH") == 0)        return 0xFFFF00;  /* 黄 */
    if (strcmp(alarm, "ULTRA_LOW") == 0)  return 0xFF0000;  /* 红 */
    if (strcmp(alarm, "ULTRA_HIGH") == 0) return 0xFF0000;  /* 红 */
    return 0xFFFFFF;
}
```

---

## 8. 数据时序

```
TC04-M(500ms轮询)     MCU         ARM屏幕
    │                  │            │
    │ CAN 0x18FEF433   │            │     ← 第1个轮胎
    │─────────────────►│            │
    │                  │ JSON推送   │
    │                  │───────────►│ 更新左前轮
    │                  │            │
    │ 500ms后          │            │
    │ CAN 0x18FEF433   │            │     ← 第2个轮胎
    │─────────────────►│            │
    │                  │───────────►│ 更新右前轮
    │                  │            │
    │ ...              │            │
    │                  │            │
    │ (6轮约3秒一轮)   │            │
    │                  │            │ 屏幕全部刷新完成
```

**注意**：TC04-M以500ms周期逐个发送各轮胎数据，6个轮胎约需3秒完成一轮刷新。ARM端收到一帧就更新对应位置，无需等待全部数据。

---

## 9. 实测数据参考

实验室无传感器时，TC04-M发出的真实数据：

| axle | tire | pressure | temp | alarm |
|------|------|----------|------|-------|
| 0 | 0 | 0 kPa | 27℃ | ULTRA_LOW |
| 0 | 3 | 0 kPa | 26℃ | ULTRA_LOW |
| 1 | 0 | 0 kPa | 27℃ | ULTRA_LOW |
| 1 | 1 | 0 kPa | 26℃ | ULTRA_LOW |
| 1 | 2 | 0 kPa | 26℃ | ULTRA_LOW |
| 1 | 3 | 0 kPa | 27℃ | ULTRA_LOW |

**装车后**，压力应为真实值（如650~900kPa），alarm应为NORMAL。

---

## 10. 故障处理

### 10.1 无数据状态

| 情况 | 显示 |
|------|------|
| 传感器无信号 | "-- kPa", 灰色, "无信号" |
| 压力=0 | "0 kPa", 红色, "ULTRA_LOW" |
| 数据超时(>5秒) | 最后值变灰, "信号丢失" |

### 10.2 报警弹窗

当收到 `alarm` ≠ `NORMAL` 时，建议：
1. 胎压页面图标变红/黄
2. 顶部状态栏显示报警图标
3. 首次报警时弹窗提示
4. 可选：语音播报

---

## 11. ARM→MCU 命令格式

ARM通过USART1发送ASCII命令，以`\r\n`或`\n`结尾，命令缓冲区64字节。

| 命令 | 格式 | MCU响应 | 说明 |
|------|------|---------|------|
| 版本查询 | `VERSION\r\n` | `TXRX_V2.0\r\n` | 查询固件版本 |
| OTA升级 | `OTA_UPGRADE\r\n` | `OTA_OK\r\n` + 复位 | 进入YMODEM升级模式 |
| 设置文本模式 | `OUTPUT_TEXT\r\n` | `MODE_OK\r\n` | 切换为文本输出 |
| 设置JSON模式 | `OUTPUT_JSON\r\n` | `MODE_OK\r\n` | 切换为JSON输出 |

---

## 12. 注意事项

1. **DMA非阻塞**：MCU使用DMA+环形缓冲区发送，不会阻塞CAN接收
2. **缓冲区限制**：TX环形缓冲区1024字节，高负载时可能丢弃数据
3. **命令缓冲区**：64字节，命令长度不能超过62字节（含\r\n）
4. **时间戳**：32位毫秒计数，约49天后溢出回零
5. **TPMS真实数据差异**：实测TC04-M的Byte7填0x00（协议要求0xFF），不影响解析
6. **压力量化误差**：8 kPa/bit分辨率，显示值可能有±8kPa误差

---

## 附录A：SQ250协议字段映射

| 信号 | 起始位 | 长度 | 分辨率 | 偏移 | 字节位置 |
|------|--------|------|--------|------|----------|
| Tire No. | 1 | 4 | — | — | Byte1[0:3] |
| Axle No. | 5 | 4 | — | — | Byte1[4:7] |
| Tire Pressure | 9 | 8 | 8 kPa/bit | 0 | Byte2 |
| Tire Temperature | 17 | 16 | 0.03125℃/bit | -273 | Byte3-4 |
| Leakage rate | 33 | 16 | 0.1 Pa/s/bit | 0 | Byte5-6 |
| Pressure alarm | 62 | 3 | — | — | Byte8[5:7] |

## 附录B：报警阈值（基于基准930kPa）

| 报警等级 | 计算公式 | 阈值 |
|----------|----------|------|
| 超高压 | 930 × 150% | 1395 kPa |
| 高压 | 930 × 130% | 1209 kPa |
| 低压 | 930 × 75% | 697.5 kPa |
| 超低压 | 930 × 50% | 465 kPa |
