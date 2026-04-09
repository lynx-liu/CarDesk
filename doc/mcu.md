# TXRX CAN-UART Bridge v2.0 上位机通讯协议规范
# ============================================================
# 文档版本: V1.0
# 日期: 2026-04-08
# 硬件平台: STM32F103C8T6 (TXRX设备)
# 通讯接口: USART1 @ 115200bps, 8N1
# ============================================================

# 1. 协议概述
# ----------------------------------------------------------
#
# 本文档定义了TXRX CAN-UART桥接设备与上位机(PC/工控机)之间的
# 串行通讯协议。TXRX设备负责从CAN总线采集J1939数据，解析后通过
# UART串口转发给上位机。
#
# 支持两种输出模式:
#   - TEXT_MODE : 人类可读的文本格式(默认)
#   - JSON_MODE : 结构化的JSON格式，便于程序解析

# 2. 物理层参数
# ----------------------------------------------------------

[physical]
interface = "UART (Serial Port)"
baud_rate = 115200        # 波特率
data_bits = 8             # 数据位
stop_bits = 1             # 停止位
parity = "None"           # 校验位
flow_control = "None"     # 流控

# 3. 协议帧结构
# ----------------------------------------------------------

# 3.1 文本模式输出格式 (默认)

[text_format]
"""
每条消息以换行符(CRLF, \\r\\n)结尾。

单行示例:
[timestamp][#sequence] [CONTROLLER] MIL:x RSL:x AWL:x PL:x
  #n SPN:xxx FMI:x OC:x Fault_Description
  ...
---

字段说明:
  timestamp  - 设备启动后的毫秒时间戳
  sequence   - 消息序列号(递增)
  CONTROLLER - 控制器名称(ABS/EBS/BCM/EMS/...)
  MIL/RSL/AWL/PL - 指示灯状态:
      0=Off, 1=On, 2=Flash, 3=NA
  n          - 故障码编号(1-based)
  SPN        - 故障参数号(Suspect Parameter Number)
  FMI        - 故障模式标识(Failure Mode Identifier)
  OC         - 发生次数(Occurrence Count)
"""

# 示例文本输出:
text_example_single = """
[12345][#1] [ABS] MIL:1 RSL:0 AWL:1 PL:0
  #1 SPN:789 FMI:3 OC:5 LF_Wheel_Spd_Sensor_GND_Short
  #2 SPN:790 FMI:5 OC:3 RF_Wheel_Spd_Sensor_VBAT_Short
---
"""

text_example_multi_frame = """
[Tp-Multi] [#15] PGN=0x00FECA SA=0x0B Size=24 Frames=4 Time=45ms
  Data Preview: 03 FF FF FF FF FF FF FF 81 15 00 00 03 FF FF FF ...
---
"""


# 3.2 JSON模式输出格式
# ----------------------------------------------------------

[json_format]
"""
JSON消息遵循RFC7159标准，每行一个完整JSON对象。
字段名使用驼峰命名法(camelCase)。

注意: JSON模式下无"---"分隔符，每行是一个独立的JSON对象。
"""

# DM1故障码JSON结构:
dm1_json_schema = {
    "type": "DM1",                    # 消息类型
    "seq": 1,                         # 序列号
    "ts": 12345,                      # 时间戳(ms)
    "id": "0x18FECA0B",               # 原始CAN ID
    "controller": "ABS",              # 控制器名称
    "lamp": {                         # 指示灯状态对象
        "MIL": 1,                     # 0=Off 1=On 2=Flash 3=NA
        "RSL": 0,
        "AWL": 1,
        "PL": 0
    },
    "faults": [                      # 故障码数组
        {
            "n": 1,                  # 编号
            "spn": 789,              # SPN值
            "fmi": 3,                # FMI值
            "oc": 5,                 # OC值
            "desc": "LF_Wheel_Spd_Sensor_GND_Short"
        },
        {
            "n": 2,
            "spn": 790,
            "fmi": 5,
            "oc": 3,
            "desc": "RF_Wheel_Spd_Sensor_VBAT_Short"
        }
    ]
}

# 多帧TP完成JSON结构:
tp_json_schema = {
    "type": "TP_MULTI_FRAME",         # 消息类型
    "seq": 15,                        # 序列号
    "ts": 12580,                      # 时间戳
    "pgn": "0x00FECA",               # 参数组号
    "sa": "0x0B",                    # 源地址
    "da": "0xFF",                    # 目标地址(0xFF=广播)
    "size": 24,                       # 数据长度(字节)
    "frames": 4,                      # 帧数
    "data": "03FF...FF..."            # 十六进制数据字符串
}


# 示例JSON输出:
json_example_dm1 = '{"type":"DM1","seq":1,"ts":12345,"id":"0x18FECA0B","controller":"ABS","lamp":{"MIL":1,"RSL":0,"AWL":1,"PL":0},"faults":[{"n":1,"spn":789,"fmi":3,"oc":5,"desc":"LF_Wheel_Spd_Sensor_GND_Short"}]}\r\n'

json_example_tp = '{"type":"TP_MULTI_FRAME","seq":15,"ts":12580,"pgn":"0x00FECA","sa":"0x0B","da":"0xFF","size":24,"frames":4,"data":"03FFFFFFFFFFFF8115000003FFFFFFFF"}\r\n'


# 4. 支持的消息类型
# ----------------------------------------------------------

[message_types]
# type字段可能的值:
DM1_SINGLE       = "DM1"              # 单帧DM1诊断报文
DM1_MULTI_FRAME  = "TP_MULTI_FRAME"   # 多帧传输完成通知


# 5. 支持的控制器列表
# -----------------------------------------------------------

[controllers]
# 控制器名称 | CAN ID (单帧) | 说明
ABS  = {"id": "0x18FECA0B", "desc": "防抱死制动系统"}
EBS  = {"id": "0x18FECAE8", "desc": "电子制动预警系统"}
BCM  = {"id": "0x18FECA21", "desc": "车身控制模块"}
EMS  = {"id": "0x18FEE700", "desc": "发动机管理系统"}
TCU  = {"id": "0x18FEF600", "desc": "变速箱控制单元"}
VCU  = {"id": "0x18FEF101", "desc": "整车控制单元"}
ICU  = {"id": "0x18FEC400", "desc": "仪表盘集群"}
EPS  = {"id": "0x18FEF002", "desc": "电子助力转向"}
SCR  = {"id": "0x18FEE500", "desc": "选择性催化还原(DPF后处理)"}
DPF  = {"id": "0x18FEE501", "desc": "柴油颗粒过滤器"}
HVAC = {"id": "0x18FEFEB0", "desc": "暖通空调控制"}
IMMO = {"id": "0x18FECA98", "desc": "防盗系统"}
TEL  = {"id": "0x18FECAEE", "desc": "远程信息处理(T-BOX)"}


# 6. FMI故障模式定义表
# -----------------------------------------------------------

[fmi_codes]
"""
FMI (Failure Mode Identifier) 定义参考SAE J1939-73:

值 | 含义
-- | -----
 0 | 条件正常 / 数据有效但低于正常操作范围
 1 | 数据有效但高于正常操作范围
 2 | 数据不稳定 / 间歇性错误
 3 | 电压低于正常范围或对地短路
 4 | 电压高于正常范围或对电源短路
 5 | 电流低于正常范围或开路
 6 | 电流高于正常范围或短路
 7 | 机械系统响应不正确
 8 | 机械系统响应时间不合理
 9 | 部件异常 / 异常变化率
10 | 超出可调范围
11 | 坏器件(非智能)
12 | 智能器件故障(内部诊断)
13 | 超出校准
14 | 特殊指令(制造商自定义)
15 | 数据漂移(从高到低)


# 7. J1939多帧传输(TP)协议说明
# -------------------------------------------------------------

[tp_protocol]
"""
当ECU需要发送超过8字节的数据时，使用J1939 TP协议:

TP.CM (连接管理) - PGN: 0xEC00 (60160)
-----------------------------------------
消息类型:
  0x10 RTS - Request To Send (请求发送)
  0x11 CTS - Clear To Send (清除发送)
  0x13 EOM - End Of Message (消息结束)
  0x14 CONN - Connection Abort (中止)
  0x20 BAM - Broadcast Announce Message (广播公告)

RTS/BAM格式:
  Byte 0: 消息类型(0x10/0x20)
  Byte 1-2: 总字节数(Little Endian)
  Byte 3: 总帧数
  Byte 4: 建议块大小(RTS)/保留(BAM)
  Byte 5-7: PGN(Little Endian)

TP.DT (数据传输) - PGN: 0xEB00 (60161)
----------------------------------------
  Byte 0: 序列号(1~255, 循环递增)
  Byte 1-7: 有效载荷(最多7字节)

超时配置:
  T1 (BS超时):   750ms  (块等待)
  T2 (TS超时):   500ms  (帧间间隔)
  T3 (TA超时):   200ms  (应答延迟)
  T4 (Th超时):   1050ms (最大会话时间)

本设备仅实现接收方向(TP RX):
  - 支持BAM广播模式
  - 支持RTS点对点模式(被动接收)
  - 最大支持1785字节数据
  - 支持4个并发会话
"""


# 8. 上位机开发指南
# -----------------------------------------------------------

[development_guide]
"""
1. 串口配置:
   - 打开串口: COMx @ 115200-8-N-1
   - 设置读取超时: 推荐100ms
   - 使用缓冲区读取避免丢包

2. 数据解析流程:
   
   a) 文本模式:
      - 按\\r\\n分割获取完整行
      - 首行包含控制器和指示灯信息
      - 后续行为故障码详情
      - "---"为消息结束标记
   
   b) JSON模式:
      - 按\\r\\n分割获取完整行
      - 使用JSON解析库解析每行
      - 根据"type"字段区分消息类型
      - "DM1": 解析faults数组
      - "TP_MULTI_FRAME": 解析data十六进制

3. 错误处理:
   - 串口断线: 检测并重连
   - 数据不完整: 等待下一个\\r\n重新同步
   - 校验和错误: 本协议无校验和，依赖UART硬件校验

4. 性能建议:
   - 高负载场景建议使用独立线程读取串口
   - 解析和处理放在工作线程
   - 缓冲区建议>=1024字节
"""


# 9. 完整通信示例
# -----------------------------------------------------------

[example_session]
"""
=== 上电启动 ===

设备输出:
====================================================
  TXRX CAN-UART Bridge v2.0 (Optimized Edition)
----------------------------------------------------
  Hardware: STM32F103C8T6 @ 72MHz
  CAN:      250Kbps, Accept ALL, Dual FIFO
  Buffer:   32 messages (ring buffer)
  USART1:   115200 bps (to Host PC)
  USART2:   115200 bps (Debug Console)
----------------------------------------------------
  Features:
    [+] J1939 Single-frame DM1 Parser
    [+] J1939 Multi-frame TP (BAM/RTS/CTS)
    [+] Text + JSON dual output mode
    [+] Extended fault code library
    [+] Real-time statistics
----------------------------------------------------
  Supported Controllers (DM1):
    - ABS  Anti-lock Brake System (0x18FECA0B)
    - EBS  Electronic Brake System (0x18FECAE8)
    - BCM  Body Control Module   (0x18FECA21)
...
====================================================

[System] Ready! Waiting for CAN data...

=== 正常运行 (接收到CAN数据) ===

[512][#1] [EMS] MIL:1 RSL:0 AWL:0 PL:0
  #1 SPN:91 FMI:3 OC:12 Engine_Oil_Pressure_Low
  #2 SPN:110 FMI:3 OC:5 Ambient_Air_Temp_Fault
---
[520][#2] [ABS] MIL:0 RSL:0 AWL:0 PL:0
  No Active Faults
---
[1532][#3] [BCM] MIL:0 RSL:0 AWL:1 PL:0
  #1 SPN:522700 FMI:5 OC:3 Turn_Lamp_Open_Circuit
---
[Tp-Multi] [#4] PGN=0x00FECA SA=0x0B Size=44 Frames=7 Time=28ms
  Data Preview: 03 FF FF FF FF FF FF FF 81 15 00 00 03 FF FF ...
---

(注: 以上为TEXT_MODE输出，JSON_MODE见下方)

=== JSON模式输出示例 ===

{"type":"DM1","seq":1,"ts":512,"id":"0x18FEE700","controller":"EMS",
"lamp":{"MIL":1,"RSL":0,"AWL":0,"PL":0},
"faults":[{"n":1,"spn":91,"fmi":3,"oc":12,"desc":"Engine_Oil_Pressure_Low"},
          {"n":2,"spn":110,"fmi":3,"oc":5,"desc":"Ambient_Air_Temp_Fault"}]}
{"type":"DM1","seq":2,"ts":520,"id":"0x18FECA0B","controller":"ABS",
"lamp":{"MIL":0,"RSL":0,"AWL":0,"PL":0},"faults":[]}
{"type":"DM1","seq":3,"ts":1532,"id":"0x18FECA21","controller":"BCM",
"lamp":{"MIL":0,"RSL":0,"AWL":1,"PL":0},
"faults":[{"n":1,"spn":522700,"fmi":5,"oc":3,"desc":"Turn_Lamp_Open_Circuit"}]}
{"type":"TP_MULTI_FRAME","seq":4,"ts":1560,"pgn":"0x00FECA",
"sa":"0x0B","da":"0xFF","size":44,"frames":7",
"data":"03FFFFFFFFFFFF8115000003FFFFFFFF..."}

"""


# 10. 版本历史
# -----------------------------------------------------------

[changelog]
v1.0 (2026-04-08):
  - 初始版本发布
  - 支持TEXT和JSON双格式
  - 支持单帧和多帧DM1
  - 包含14个FAW J6控制器定义
  - 包含200+个故障码描述
