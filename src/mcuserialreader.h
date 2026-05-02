#ifndef MCUSERIALREADER_H
#define MCUSERIALREADER_H

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QVector>

class QSerialPort;

// 单条故障信息（来自 MCU DM1 报文）
struct McuFaultInfo {
    int     spn;      // Suspect Parameter Number
    int     fmi;      // Failure Mode Identifier
    int     oc;       // Occurrence Count
    QString rawDesc;  // MCU 原始英文描述（作为备用显示）
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

    // 打开串口，默认 /dev/ttyS2；成功返回 true
    bool open(const QString &portName = QStringLiteral("/dev/ttyS2"));
    void close();
    bool isOpen() const;

signals:
    // 每次解析完整 DM1 块后发射（faults 为空表示该控制器无故障）
    void dm1Received(const QString &controller, const QVector<McuFaultInfo> &faults);
    // LC 灯光指令：右转/左转/倒车 (0=OFF, 1=ON)
    void lcReceived(int rTurn, int lTurn, int backup);
    // TD 时间日期：年月日时分（已解码，可直接使用）
    void tdReceived(int year, int month, int day, int hour, int min);

private slots:
    void onReadyRead();

private:
    void processLine(const QByteArray &line);
    void parseJsonLine(const QByteArray &line);

    static McuSerialReader *s_shared;
    QSerialPort          *m_port;
    QByteArray            m_buf;
    // 解析状态
    bool                  m_inBlock;
    QString               m_curController;
    QVector<McuFaultInfo> m_curFaults;
};

#endif // MCUSERIALREADER_H
