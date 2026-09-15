#ifndef MCUSERIALREADER_H
#define MCUSERIALREADER_H

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QString>
#include <QVector>

class QSerialPort;
class QTimer;

// 单条故障信息（来自 MCU DM1 报文）
struct McuFaultInfo {
    int     spn;      // Suspect Parameter Number
    int     fmi;      // Failure Mode Identifier
    int     oc;       // Occurrence Count
    QString rawDesc;  // MCU 原始英文描述（作为备用显示）
};

// 胎压一条（MCU TPMS JSON/TEXT）
struct McuTpmsInfo {
    int     axle = 0;           // 0=前桥 … 6=备胎
    int     tire = 0;           // 0~3 从左到右
    int     pressureKpa = 0;
    float   temperatureC = 0.f;
    float   leakagePaS = 0.f;
    QString alarm;              // NORMAL/LOW/HIGH/ULTRA_LOW/ULTRA_HIGH
    quint32 mcuTsMs = 0;        // MCU 上电毫秒
};

/**
 * @brief 从 /dev/ttyS2 (115200,8N1) 读取 MCU TEXT 格式 DM1 输出，
 *        解析后通过 dm1Received 信号发出故障列表。
 *
 * TEXT 格式参见 mcu.md:
 *   [ts][#seq] [CONTROLLER] MIL:x RSL:x AWL:x PL:x
 *     #1 SPN:xxx FMI:x OC:x Description
 *   ---
 */
class McuSerialReader : public QObject {
    Q_OBJECT
public:
    explicit McuSerialReader(QObject *parent = nullptr);
    ~McuSerialReader() override;

    // 获取/创建全局共享实例（首次调用时以 parent 为父对象）
    static McuSerialReader *ensureShared(QObject *parent = nullptr);
    static McuSerialReader *existingShared();

    // 布局释放：清空 MCU 侧转向/倒车缓存并同步为全 OFF（避免下一帧把已退出的信号又拉起来）
    void clearCanSignalState();

    // 打开串口，默认 /dev/ttyS2；成功返回 true
    bool open(const QString &portName = QStringLiteral("/dev/ttyS2"));
    void close();
    bool isOpen() const;

    // 向串口写数据（二进制安全：写全并等待刷出；日志不打印整包二进制）
    void write(const QByteArray &data);

    // 切换升级模式：true=升级中（onReadyRead 将原始数据通过 rawDataReceived 信号转发，不做文本解析）
    void setUpgradeMode(bool mode);

    // 丢弃串口接收缓冲（进入 YMODEM 前清掉 CAN/文本残留）
    void discardInput();

signals:
    // 每次解析完整 DM1 块后发射（faults 为空表示该控制器无故障）
    void dm1Received(const QString &controller, const QVector<McuFaultInfo> &faults);
    // LC 灯光指令：右转/左转/倒车 (0=OFF, 1=ON)
    void lcReceived(int rTurn, int lTurn, int backup);
    // TD 时间日期：年月日时分（已解码，可直接使用）
    void tdReceived(int year, int month, int day, int hour, int min);
    // 胎压一条（JSON type=TPMS 或 TEXT [TPMS]）
    void tpmsReceived(const McuTpmsInfo &info);
    // 升级模式下的原始接收数据
    void rawDataReceived(const QByteArray &data);

private slots:
    void onReadyRead();

private:
    void processLine(const QByteArray &line);
    void parseJsonLine(const QByteArray &line);
    // 解析 VIST TEXT 行（[ts][#seq][NAME] key=value ...）
    void parseVistTextLine(const QString &name, const QString &kv);
    void emitLcIfChanged();
    void noteTpmsLeak(const McuTpmsInfo &info);
    void refreshTpmsLeakWarning();

    static McuSerialReader *s_shared;
    QSerialPort          *m_port;
    QByteArray            m_buf;
    bool                  m_upgradeMode;
    // DM1 块解析状态
    bool                  m_inBlock;
    QString               m_curController;
    QVector<McuFaultInfo> m_curFaults;
    // LC/OEL 跨报文状态：OEL 报转向灯，LC 报倒车，需合并后一起 emit
    int                   m_canRTurn;     // 来自 OEL（或 OEL 缺失时来自 LC）
    int                   m_canLTurn;     // 来自 OEL（或 OEL 缺失时来自 LC）
    int                   m_canBackup;    // 来自 LC
    bool                  m_oelReceived;  // 曾收到过 OEL 报文，则忽略 LC 转向字段
    int                   m_lastEmittedRTurn = -1;
    int                   m_lastEmittedLTurn = -1;
    int                   m_lastEmittedBackup = -1;

    struct TpmsLeakSlot {
        float leakagePaS = 0.f;
        qint64 lastRxMs = 0;
    };
    QHash<quint32, TpmsLeakSlot> m_tpmsLeakSlots;
    QTimer *m_tpmsLeakTimer = nullptr;
    bool m_tpmsLeakActive = false;
};

Q_DECLARE_METATYPE(McuTpmsInfo)

#endif // MCUSERIALREADER_H
