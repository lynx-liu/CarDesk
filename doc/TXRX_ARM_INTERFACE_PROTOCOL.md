# TXRX -> ARM 通讯接口协议说明

- 版本: V2.0
- 日期: 2026-04-29
- 硬件: STM32F103C8T6 (TXRX) <-> ARM (上位机)
- 基于: FAW J6 质惠版 VIST CAN矩阵 V1.3

---

## 1. 物理层

| 参数     | 值                    |
|----------|----------------------|
| 接口     | USART1 (TX:PA9, RX:PA10) |
| 波特率   | 115200 bps           |
| 数据位   | 8                    |
| 停止位   | 1                    |
| 校验位   | None                 |
| 流控     | None                 |
| CAN总线  | 250Kbps, J1939扩展帧 |

> USART2 为调试串口, 仅供开发调试, 不作为通讯接口.

---

## 2. 消息总览

TXRX 通过 USART1 向 ARM 发送两类消息:

| 消息类型     | type字段值        | 说明                      | 触发来源              |
|-------------|-------------------|--------------------------|----------------------|
| 车辆数据     | `VIST`            | 22个CAN ID的实时车辆信号   | CAN周期报文           |
| DM1诊断      | `DM1`             | 单帧/多帧故障诊断信息      | DM1 CAN报文           |
| 多帧传输     | `TP_MULTI_FRAME`  | J1939 TP多帧完成通知      | TP接收完成回调        |

每条消息以 `\r\n` (CRLF) 结尾.

---

## 3. TEXT 模式 (默认)

### 3.1 车辆数据格式

```
[timestamp][#sequence][NAME] key=value\r\n
```

**示例:**
```
[512][#1][ETC2] Gear=6(D)
[520][#2][TCO1] Speed=80.50km/h
[530][#3][LC] R-Turn=OFF L-Turn=ON Backup=OFF Marker=ON
---
```

### 3.2 DM1诊断格式

```
[timestamp][#sequence] [CTRL] MIL:x RSL:x AWL:x PL:x\r\n
  #n SPN:xxx FMI:x OC:x Description\r\n
---\r\n
```

**示例:**
```
[1024][#10] [ABS] MIL:0 RSL:1 AWL:0 PL:0
  #1 SPN:789 FMI:3 OC:5 SPN789_FMI3_LeftFrontWheelSpeedSensorFault
  #2 SPN:790 FMI:5 OC:3 SPN790_FMI5_RightFrontWheelSpeedSensorFault
---
[1050][#11] [VIST] MIL:0 RSL:0 AWL:0 PL:0
  No Active Faults
---
```

### 3.3 TP多帧格式

```
[TP-Multi] [#sequence] PGN=0xXXXXX SA=0xXX Size=xxx Frames=xx Time=xxxms\r\n
  Data Preview: XX XX XX XX XX XX ...\r\n
---\r\n
```

---

## 4. JSON 模式 (推荐ARM应用使用)

### 4.1 车辆数据 JSON

每行一个独立JSON对象, 由 `"type":"VIST"` 标识:

#### 4.1.1 ETC2 - 变速箱 (0x18F00503)

```json
{"type":"VIST","seq":1,"ts":512,"id":"0x18F00503","name":"ETC2","gear":6}
```

| 字段    | 类型   | 范围         | 说明                         |
|---------|--------|-------------|------------------------------|
| gear    | int    | -125~125    | 当前档位. >0=前进, <0=倒挡, 0=空挡 |

#### 4.1.2 TCO1 - 车速 (0x0CFE6C17)

```json
{"type":"VIST","seq":2,"ts":520,"id":"0x0CFE6C17","name":"TCO1","speed_kmh":80.50}
```

| 字段       | 类型  | 单位  | 说明           |
|------------|-------|-------|----------------|
| speed_kmh  | float | km/h  | 行车记录仪车速  |

#### 4.1.3 LC - 灯光指令 (0x0CFE4121)

```json
{"type":"VIST","seq":3,"ts":530,"id":"0x0CFE4121","name":"LC","r_turn":0,"l_turn":1,"backup":0,"marker":1}
```

| 字段   | 类型  | 值      | 说明        |
|--------|-------|---------|-------------|
| r_turn | uint8 | 0/1     | 右转向灯    |
| l_turn | uint8 | 0/1     | 左转向灯    |
| backup | uint8 | 0/1     | 倒车灯/喇叭 |
| marker | uint8 | 0/1     | 标志灯      |

#### 4.1.4 CL - 驾驶室照明 (0x18D00021)

```json
{"type":"VIST","seq":4,"ts":540,"id":"0x18D00021","name":"CL","brightness_pct":60.0}
```

| 字段           | 类型  | 单位 | 说明         |
|----------------|-------|------|-------------|
| brightness_pct | float | %    | 驾驶室亮度  |

#### 4.1.5 FLI1 - 车道偏离图像(紧急) (0x10F007E8)

```json
{"type":"VIST","seq":5,"ts":550,"id":"0x10F007E8","name":"FLI1","depart_l":0,"depart_r":1,"imminent_r":0,"imminent_l":0}
```

| 字段       | 类型  | 值   | 说明             |
|------------|-------|------|------------------|
| depart_l   | uint8 | 0/1  | 左侧车道偏离     |
| depart_r   | uint8 | 0/1  | 右侧车道偏离     |
| imminent_r | uint8 | 0/1  | 右侧即将偏离     |
| imminent_l | uint8 | 0/1  | 左侧即将偏离     |

#### 4.1.6 FLI2 - 车道偏离系统 (0x18FE5BE8)

```json
{"type":"VIST","seq":6,"ts":560,"id":"0x18FE5BE8","name":"FLI2","ldw_state":2}
```

| 字段      | 类型  | 值            | 说明        |
|-----------|-------|---------------|-------------|
| ldw_state | uint8 | 0/1/2/3       | 0=未就绪 1=临时禁用 2=启用 3=关闭 |

#### 4.1.7 AEBS1 - 紧急制动 (0x0CF02FA0)

```json
{"type":"VIST","seq":7,"ts":570,"id":"0x0CF02FA0","name":"AEBS1","collision_warn":3}
```

| 字段          | 类型  | 值     | 说明        |
|---------------|-------|--------|-------------|
| collision_warn| uint8 | 0~7   | 碰撞预警等级, 0=无预警 |

#### 4.1.8 LDW_FCW1 - 车道偏离/前碰预警 (0x18FFD0E8)

```json
{"type":"VIST","seq":8,"ts":580,"id":"0x18FFD0E8","name":"LDW_FCW1","dsm_st":2,"dsm_warn":1,"spd_lim_lvl":0,"spd_lim_st":1}
```

| 字段       | 类型  | 值    | 说明                  |
|------------|-------|-------|-----------------------|
| dsm_st     | uint8 | 0~7   | DSM状态: 0=Off 1=Standby 2=On |
| dsm_warn   | uint8 | 0~7   | DSM预警等级            |
| spd_lim_lvl| uint8 | 0~7   | 限速预警等级           |
| spd_lim_st | uint8 | 0~3   | 限速状态: 0=激活 1=未激活 2=错误 |

#### 4.1.9 LDW2 - FCW灵敏度 (0x18FFD3E8)

```json
{"type":"VIST","seq":9,"ts":590,"id":"0x18FFD3E8","name":"LDW2","fcw_sens":0}
```

| 字段     | 类型  | 值   | 说明                   |
|----------|-------|------|------------------------|
| fcw_sens | uint8 | 0/1  | 0=High(高) 1=Low(低)    |

#### 4.1.10 BSD_1 - 盲区检测 (0x18FFD4A7)

```json
{"type":"VIST","seq":10,"ts":600,"id":"0x18FFD4A7","name":"BSD_1","bsd_err":0,"bsd_warn":1}
```

| 字段     | 类型  | 值    | 说明       |
|----------|-------|-------|------------|
| bsd_err  | uint8 | 0/1   | 错误状态   |
| bsd_warn | uint8 | 0~2   | 预警等级   |

#### 4.1.11 MIX_1 - 混合罐 (0x18FF4AE7)

```json
{"type":"VIST","seq":11,"ts":610,"id":"0x18FF4AE7","name":"MIX_1","mix_op":0,"hyd_oil_c":35.0}
```

| 字段      | 类型  | 值   | 说明                                  |
|-----------|-------|------|---------------------------------------|
| mix_op    | uint8 | 0~3  | 0=正转 1=反转 2=停止 3=不可用        |
| hyd_oil_c | float | °C   | 液压马达回油温度                      |

#### 4.1.12 EMS_5 - 发动机混合 (0x19FF3000)

```json
{"type":"VIST","seq":12,"ts":620,"id":"0x19FF3000","name":"EMS_5","rot_rpm":12.5,"mix_op":0,"slump_pct":3.2,"rot_total":12345,"hyd_oil_c":35.0}
```

| 字段       | 类型  | 单位  | 说明                              |
|------------|-------|-------|-----------------------------------|
| rot_rpm    | float | r/min | 混合罐转速 (原始值 x 0.25)       |
| mix_op     | uint8 | -     | 运行状态 (同MIX_1)                |
| slump_pct  | float | %     | 混凝土塌落度变化 (原始值 x 0.4)  |
| rot_total  | uint32| r     | 混合罐总转数                      |
| hyd_oil_c  | float | °C    | 液压马达回油温度                  |

#### 4.1.13 EMS_MIX1 - 发动机混合1 (0x18FF4A00)

```json
{"type":"VIST","seq":13,"ts":630,"id":"0x18FF4A00","name":"EMS_MIX1","rot_rpm":12.5,"mix_op":0,"rot_total":12345,"hyd_oil_c":35.0}
```

| 字段      | 类型   | 单位  | 说明               |
|-----------|--------|-------|--------------------|
| rot_rpm   | float  | r/min | 混合罐转速         |
| mix_op    | uint8  | -     | 运行状态           |
| rot_total | uint32 | r     | 总转数             |
| hyd_oil_c | float  | °C    | 回油温度           |

#### 4.1.14 RCM_SC1 - 雷达开关指令 (0x19FF2183)

```json
{"type":"VIST","seq":14,"ts":640,"id":"0x19FF2183","name":"RCM_SC1","mix_cmd":3}
```

| 字段    | 类型  | 值     | 说明                                                |
|---------|-------|--------|-----------------------------------------------------|
| mix_cmd | uint8 | 0~8    | 0=进料 1=卸料 2=搅拌 3=停止 4=点动+ 5=点动- 6=连续+ 7=连续- 8=急停 |

#### 4.1.15 VIST_SC1 - VIST开关指令 (0x19FF2141)

```json
{"type":"VIST","seq":15,"ts":650,"id":"0x19FF2141","name":"VIST_SC1","volume_m3":6.5,"mix_cmd":3}
```

| 字段      | 类型  | 单位 | 说明      |
|-----------|-------|------|-----------|
| volume_m3 | float | m³   | 混凝土体积 (原始值 x 0.5) |
| mix_cmd   | uint8 | -    | 混合罐指令 (同RCM_SC1)    |

#### 4.1.16 AVM_1 - 全景影像 (0x18FFCF28)

```json
{"type":"VIST","seq":16,"ts":660,"id":"0x18FFCF28","name":"AVM_1","avm_mode":1,"bsd_st":2,"bsd_l":0,"bsd_r":1}
```

| 字段     | 类型  | 值    | 说明                                      |
|----------|-------|-------|-------------------------------------------|
| avm_mode | uint8 | 0~11  | 0=OFF 1=前 2=后 3=左 4=右 5=前右 6=前左 7=后右 8=后左 9=俯视 10=环视 11=3D |
| bsd_st   | uint8 | 0~7   | BSD系统状态                                |
| bsd_l    | uint8 | 0~7   | BSD左侧预警等级                            |
| bsd_r    | uint8 | 0~7   | BSD右侧预警等级                            |

#### 4.1.17 SC_VIST2 - VIST指令2 (0x18FF4341)

```json
{"type":"VIST","seq":17,"ts":670,"id":"0x18FF4341","name":"SC_VIST2","y_px":480,"coord":1,"screenshot":0}
```

| 字段      | 类型  | 单位 | 说明              |
|-----------|-------|------|-------------------|
| y_px      | uint16| px   | AVM Y坐标指令     |
| coord     | uint8 | 0~3  | AVM坐标指令       |
| screenshot| uint8 | 0~3  | AVM截屏指令       |

#### 4.1.18 TD - 时间日期 (0x18FEE641 / 0x18FEE64A)

```json
{"type":"VIST","seq":18,"ts":680,"id":"0x18FEE641","name":"TD_VIST","year":2026,"month":4,"day":29,"hour":23,"min":15}
```

| 字段   | 类型  | 单位  | 说明                     |
|--------|-------|-------|--------------------------|
| year   | uint16| year  | 年 (原始值 + 1985)       |
| month  | uint8 | month | 月 (1~12)                |
| day    | uint8 | days  | 日 (原始值 x 0.25)       |
| hour   | uint8 | h     | 时 (0~23)                |
| min    | uint8 | min   | 分 (0~59)                |

#### 4.1.19 OEL - 外部灯光 (0x0CFDCC21)

```json
{"type":"VIST","seq":19,"ts":690,"id":"0x0CFDCC21","name":"OEL","turn_sig":1}
```

| 字段     | 类型  | 值  | 说明                      |
|----------|-------|-----|---------------------------|
| turn_sig | uint8 | 0~7 | 0=无 1=左转 2=右转 6=错误 |

#### 4.1.20 SC_VIST - VIST开关 (0x18FF0241)

```json
{"type":"VIST","seq":20,"ts":700,"id":"0x18FF0241","name":"SC_VIST","avm_sw":1}
```

| 字段   | 类型  | 值   | 说明          |
|--------|-------|------|---------------|
| avm_sw | uint8 | 0~3  | AVM开关状态   |

---

### 4.2 DM1 诊断 JSON

```json
{"type":"DM1","seq":100,"ts":1024,"id":"0x18FECA0B","controller":"ABS","lamp":{"MIL":0,"RSL":1,"AWL":0,"PL":0},"faults":[{"n":1,"spn":789,"fmi":3,"oc":5,"desc":"SPN789_FMI3_LeftFrontWheelSpeedSensorFault"}]}
```

| 字段       | 类型   | 说明                                   |
|------------|--------|----------------------------------------|
| type       | string | 固定 `"DM1"`                           |
| seq        | uint32 | 消息序列号(递增)                        |
| ts         | uint32 | 时间戳(设备启动后毫秒数)               |
| id         | string | 原始CAN ID (8位hex字符串)              |
| controller | string | 控制器名称: `ABS`/`DUAL_WARN`/`BCM`/`VIST` |
| lamp       | object | 指示灯状态                             |
| lamp.MIL   | int    | 0=Off 1=On 2=Flash 3=NA               |
| lamp.RSL   | int    | 红色停止灯                              |
| lamp.AWL   | int    | 琥珀色警告灯                            |
| lamp.PL    | int    | 保护灯                                  |
| faults     | array  | 故障码数组(最多5条,单帧)               |
| faults[].n | int    | 编号(1-based)                           |
| faults[].spn | uint32| SPN值(19-bit)                          |
| faults[].fmi | uint8 | FMI值(5-bit)                           |
| faults[].oc  | uint8 | OC值(7-bit)                            |
| faults[].desc| string| 故障描述(英文)                         |

#### 4.2.1 支持的DM1控制器

| 控制器    | 单帧CAN ID   | TP.CM CAN ID | TP.DT CAN ID |
|-----------|-------------|--------------|--------------|
| ABS       | 0x18FECA0B   | 0x1CECFF0B   | 0x1CEBFF0B   |
| DUAL_WARN | 0x18FECAE8   | 0x1CECFFE8   | 0x1CEBFFE8   |
| BCM       | 0x18FECA21   | 0x1CECFF21   | 0x1CEBFF21   |
| VIST      | 0x18FECA41   | -            | -            |

#### 4.2.2 指示灯状态值定义 (SAE J1939-71)

| 值 | 含义           |
|----|---------------|
| 0  | Off (关闭)     |
| 1  | On (常亮)      |
| 2  | Flash (闪烁)   |
| 3  | Not Available  |

#### 4.2.3 FMI 定义 (SAE J1939-73)

| 值  | 含义                    |
|-----|------------------------|
| 0   | 数据有效但低于正常范围    |
| 1   | 数据有效但高于正常范围    |
| 2   | 数据不稳定/间歇性错误    |
| 3   | 电压低于正常/对地短路    |
| 4   | 电压高于正常/对电源短路  |
| 5   | 电流低于正常/开路        |
| 6   | 电流高于正常/短路        |
| 7   | 机械系统响应不正确        |
| 8   | 机械系统响应时间不合理    |
| 9   | 部件异常/变化率异常      |
| 10  | 超出可调范围             |
| 11  | 坏器件(非智能)           |
| 12  | 智能器件内部故障          |
| 13  | 超出校准                 |
| 14  | 制造商自定义              |
| 15  | 数据漂移(高到低)          |

### 4.3 TP多帧 JSON

```json
{"type":"TP_MULTI_FRAME","seq":200,"ts":1560,"pgn":"0x00FECA","sa":"0x0B","da":"0xFF","size":44,"frames":7,"data":"03FFFFFFFFFFFF8115000003FFFFFFFF..."}
```

| 字段   | 类型   | 说明                          |
|--------|--------|-------------------------------|
| pgn    | string | 参数组号(5位hex)              |
| sa     | string | 源地址(2位hex)                |
| da     | string | 目标地址(2位hex), 0xFF=广播   |
| size   | int    | 总数据长度(字节)              |
| frames | int    | 总帧数                        |
| data   | string | 十六进制数据字符串(最多显示128字节) |

---

## 5. JSON 通用字段说明

所有JSON消息共享以下公共字段:

| 字段 | 类型   | 说明                          |
|------|--------|-------------------------------|
| type | string | 消息类型: `VIST`/`DM1`/`TP_MULTI_FRAME` |
| seq  | uint32 | 消息序列号(设备启动后递增, 不跨类型) |
| ts   | uint32 | 时间戳(设备启动后毫秒数, HAL_GetTick()) |

---

## 6. ARM端开发指南

### 6.1 串口初始化

```
波特率: 115200
数据位: 8
停止位: 1
校验: None
流控: None
读超时: 100ms (推荐)
接收缓冲区: >= 1024 字节
```

### 6.2 数据解析流程

```
1. 打开串口
2. 循环读取数据
3. 按 \r\n 分割, 获取完整行
4. 解析JSON: 提取 type 字段
   - "VIST" -> 车辆数据, 按 id/name 字段分发
   - "DM1"  -> 诊断数据, 解析 lamp + faults 数组
   - "TP_MULTI_FRAME" -> 多帧数据, 解析 data hex字符串
5. 处理业务逻辑
```

### 6.3 解析伪代码 (C语言)

```c
// 串口读取线程
void uart_read_thread(void) {
    char line_buf[1024];
    int line_pos = 0;

    while (running) {
        char ch;
        int n = read(uart_fd, &ch, 1);
        if (n <= 0) continue;

        line_buf[line_pos++] = ch;

        if (ch == '\n' && line_pos > 1) {
            line_buf[line_pos] = '\0';
            process_line(line_buf);
            line_pos = 0;
        }
        if (line_pos >= sizeof(line_buf) - 1) {
            line_pos = 0; // 溢出保护, 重新同步
        }
    }
}

// 消息分发
void process_line(char *line) {
    // 跳过 \r
    int len = strlen(line);
    while (len > 0 && (line[len-1] == '\r' || line[len-1] == '\n'))
        line[--len] = '\0';

    if (len == 0) return;

    // 检查是否为JSON
    if (line[0] == '{') {
        parse_json_message(line);
    } else if (line[0] == '[') {
        // TEXT模式, 按需解析
        parse_text_message(line);
    }
}

// JSON消息解析
void parse_json_message(const char *json) {
    char type[32];
    if (!json_extract_string(json, "type", type, sizeof(type))) return;

    if (strcmp(type, "VIST") == 0) {
        char name[32];
        json_extract_string(json, "name", name, sizeof(name));

        if (strcmp(name, "ETC2") == 0) {
            int gear = json_extract_int(json, "gear");
            // 更新UI: 档位
        } else if (strcmp(name, "TCO1") == 0) {
            double speed = json_extract_double(json, "speed_kmh");
            // 更新UI: 车速
        } else if (strcmp(name, "LC") == 0) {
            // 灯光指令
        }
        // ... 其他21个ID
    }
    else if (strcmp(type, "DM1") == 0) {
        char controller[32];
        json_extract_string(json, "controller", controller, sizeof(controller));

        // 解析指示灯
        int mil = json_extract_int_by_path(json, "lamp.MIL");
        int rsl = json_extract_int_by_path(json, "lamp.RSL");

        // 解析故障码数组
        // faults 是JSON数组: "faults":[{...},{...}]
    }
    else if (strcmp(type, "TP_MULTI_FRAME") == 0) {
        // 多帧数据处理
    }
}
```

### 6.4 注意事项

1. **序列号不区分类型**: seq 在所有消息类型间共享, 单调递增
2. **时间戳**: ts 是设备启动后的毫秒数, 非绝对时间, 可用于计算消息间隔
3. **无校验和**: 协议层无校验, 依赖UART硬件校验(奇偶/帧错)
4. **数据丢失**: 高CAN负载时USART1可能来不及发送, 建议ARM端做好容错
5. **JSON缓冲区**: 单条JSON最长约512字节, 接收缓冲区建议 >= 1024
6. **实时性**: 车辆数据消息频率取决于CAN总线周期, 一般 50ms~1000ms
7. **DM1多帧**: 故障码超过5个时会触发TP多帧传输, 通过 `TP_MULTI_FRAME` 消息接收
8. **Unknown故障码**: 若SPN/FMI不在故障码表中, desc字段为 `"Unknown_SPNxxx_FMIx"` 格式

---

## 7. 完整通讯会话示例

```
[设备上电]

TXRX开机信息 (TEXT模式, 仅输出到USART2调试口):
====================================================
  FAW J6 VIST CAN Parser (22 CAN IDs, V1.3)
  [+] Text + JSON dual output mode
  [+] Extended fault code library
  [+] Real-time statistics
  Supported Controllers (DM1 Single+Multi-frame):
    - ABS       Anti-lock Brake System   (0x18FECA0B)
    - DUAL_WARN Dual Warning System      (0x18FECAE8)
    - BCM       Body Control Module      (0x18FECA21)
    - VIST      Vehicle Info & Safety    (0x18FECA41)
====================================================
[System] Ready! Waiting for CAN data...

[正常运行, ARM通过USART1接收到:]

{"type":"VIST","seq":1,"ts":512,"id":"0x18F00503","name":"ETC2","gear":6}
{"type":"VIST","seq":2,"ts":520,"id":"0x0CFE6C17","name":"TCO1","speed_kmh":80.50}
{"type":"VIST","seq":3,"ts":530,"id":"0x0CFE4121","name":"LC","r_turn":0,"l_turn":1,"backup":0,"marker":1}
{"type":"VIST","seq":4,"ts":540,"id":"0x18FE5BE8","name":"FLI2","ldw_state":2}
{"type":"DM1","seq":5,"ts":1024,"id":"0x18FECA0B","controller":"ABS","lamp":{"MIL":0,"RSL":1,"AWL":0,"PL":0},"faults":[{"n":1,"spn":789,"fmi":3,"oc":5,"desc":"SPN789_FMI3_LeftFrontWheelSpeedSensorFault"}]}
{"type":"DM1","seq":6,"ts":1050,"id":"0x18FECA41","controller":"VIST","lamp":{"MIL":0,"RSL":0,"AWL":0,"PL":0},"faults":[]}
```

---

## 8. 22个车辆数据CAN ID 速查表

| #  | CAN ID     | 宏定义        | 名称      | 发送周期 |
|----|-----------|---------------|-----------|---------|
| 0  | 0x18F00503 | CANID_ETC2    | ETC2      | 100ms   |
| 1  | 0x0CFE6C17 | CANID_TCO1    | TCO1      | 50ms    |
| 2  | 0x0CFE4121 | CANID_LC      | LC        | 35ms    |
| 3  | 0x18D00021 | CANID_CL      | CL        | 100ms   |
| 4  | 0x10F007E8 | CANID_FLI1    | FLI1      | 50ms    |
| 5  | 0x18FE5BE8 | CANID_FLI2    | FLI2      | 100ms   |
| 6  | 0x0CF02FA0 | CANID_AEBS1   | AEBS1     | 50ms    |
| 7  | 0x18FFD0E8 | CANID_LDW_FCW1| LDW_FCW1  | 100ms   |
| 8  | 0x18FFD3E8 | CANID_LDW2    | LDW2      | 50ms    |
| 9  | 0x18FFD4A7 | CANID_BSD_1   | BSD_1     | 100ms   |
| 10 | 0x18FF4AE7 | CANID_MIX_1   | MIX_1     | 100ms   |
| 11 | 0x19FF3000 | CANID_EMS_5   | EMS_5     | 1000ms  |
| 12 | 0x18FF4A00 | CANID_EMS_MIX1| EMS_MIX1  | 100ms   |
| 13 | 0x19FF2183 | CANID_RCM_SC1 | RCM_SC1   | 100ms   |
| 14 | 0x19FF2141 | CANID_VIST_SC1| VIST_SC1  | 100ms   |
| 15 | 0x18FFCF28 | CANID_AVM_1   | AVM_1     | 100ms   |
| 16 | 0x18FF4341 | CANID_SC_VIST2| SC_VIST2  | 50ms    |
| 17 | 0x18FEE641 | CANID_TD_VIST | TD_VIST   | 1000ms  |
| 18 | 0x18FEE64A | CANID_TD_OTHER| TD_OTHER  | 1000ms  |
| 19 | 0x0CFDCC21 | CANID_OEL     | OEL       | 100ms   |
| 20 | 0x18FF0241 | CANID_SC_VIST | SC_VIST   | 100ms   |
| 21 | 0x18FECA41 | CANID_DM1_VIST| DM1_VIST  | 1000ms  |
