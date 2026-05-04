#include "mcuserialreader.h"
#include "appsignals.h"

#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSerialPort>

McuSerialReader *McuSerialReader::s_shared = nullptr;

McuSerialReader *McuSerialReader::ensureShared(QObject *parent)
{
    if (!s_shared) {
        s_shared = new McuSerialReader(parent);
    }
    return s_shared;
}

McuSerialReader::McuSerialReader(QObject *parent)
    : QObject(parent)
    , m_port(new QSerialPort(this))
    , m_inBlock(false)
    , m_canRTurn(0)
    , m_canLTurn(0)
    , m_canBackup(0)
{
    connect(m_port, &QSerialPort::readyRead, this, &McuSerialReader::onReadyRead);
}

McuSerialReader::~McuSerialReader()
{
    close();
}

bool McuSerialReader::open(const QString &portName)
{
    m_port->setPortName(portName);
    m_port->setBaudRate(QSerialPort::Baud115200);
    m_port->setDataBits(QSerialPort::Data8);
    m_port->setParity(QSerialPort::NoParity);
    m_port->setStopBits(QSerialPort::OneStop);
    m_port->setFlowControl(QSerialPort::NoFlowControl);
    const bool ok = m_port->open(QIODevice::ReadOnly);
    if (ok)
        qDebug() << "[MCU] Serial port opened:" << portName;
    else
        qWarning() << "[MCU] Failed to open serial port:" << portName << m_port->errorString();
    return ok;
}

void McuSerialReader::close()
{
    if (m_port && m_port->isOpen())
        m_port->close();
}

bool McuSerialReader::isOpen() const
{
    return m_port && m_port->isOpen();
}

void McuSerialReader::onReadyRead()
{
    m_buf += m_port->readAll();
    int pos;
    while ((pos = m_buf.indexOf('\n')) != -1) {
        QByteArray line = m_buf.left(pos);
        m_buf = m_buf.mid(pos + 1);
        // 去掉末尾 \r
        if (!line.isEmpty() && line.back() == '\r')
            line.chop(1);
        if (!line.isEmpty())
            qDebug() << "[MCU] RAW:" << line;
        processLine(line);
    }
}

void McuSerialReader::parseJsonLine(const QByteArray &raw)
{
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return;
    const QJsonObject obj = doc.object();
    if (obj.value(QStringLiteral("type")).toString() != QLatin1String("VIST")) return;
    const QString name = obj.value(QStringLiteral("name")).toString();
    if (name == QLatin1String("OEL")) {
        // 0x0CFDCC21: turn_sig 0=无 1=左转 2=右转
        const int turnSig = obj.value(QStringLiteral("turn_sig")).toInt();
        m_canLTurn = (turnSig == 1) ? 1 : 0;
        m_canRTurn = (turnSig == 2) ? 1 : 0;
        qDebug() << "[TXRX] OEL turn_sig=" << turnSig
                 << "lTurn=" << m_canLTurn << "rTurn=" << m_canRTurn;
        emit lcReceived(m_canRTurn, m_canLTurn, m_canBackup);
    } else if (name == QLatin1String("LC")) {
        // 0x0CFE4121: backup 倒车灯
        const int rTurn  = obj.value(QStringLiteral("r_turn")).toInt();
        const int lTurn  = obj.value(QStringLiteral("l_turn")).toInt();
        const int backup = obj.value(QStringLiteral("backup")).toInt();
        m_canBackup = backup;
        // LC 也可能包含转向信息，但以 OEL 为主。若无 OEL 报文时用 LC 的转向字段作备用
        if (m_canLTurn == 0 && m_canRTurn == 0) {
            m_canLTurn = lTurn;
            m_canRTurn = rTurn;
        }
        qDebug() << "[TXRX] LC r_turn=" << m_canRTurn << "l_turn=" << m_canLTurn << "backup=" << m_canBackup;
        emit lcReceived(m_canRTurn, m_canLTurn, m_canBackup);
    } else if (name == QLatin1String("TCO1")) {
        const float speedKmh = static_cast<float>(obj.value(QStringLiteral("speed_kmh")).toDouble());
        qDebug() << "[TXRX] TCO1 speed_kmh=" << speedKmh;
        emit AppSignals::instance()->vehicleSpeedChanged(speedKmh);
    } else if (name == QLatin1String("TD_VIST") || name == QLatin1String("TD_OTHER")) {
        const int year  = obj.value(QStringLiteral("year")).toInt();
        const int month = obj.value(QStringLiteral("month")).toInt();
        const int day   = obj.value(QStringLiteral("day")).toInt();
        const int hour  = obj.value(QStringLiteral("hour")).toInt();
        const int min   = obj.value(QStringLiteral("min")).toInt();
        qDebug() << "[TXRX] TD" << name << year << month << day << hour << min;
        emit tdReceived(year, month, day, hour, min);
    }
}

void McuSerialReader::processLine(const QByteArray &raw)
{
    // JSON 行以 '{' 开头，直接交由 parseJsonLine 处理
    if (!raw.isEmpty() && raw.at(0) == '{') {
        parseJsonLine(raw);
        return;
    }

    // MCU 输出均为 ASCII，从 Latin-1 解码即可
    const QString line = QString::fromLatin1(raw);

    if (!m_inBlock) {
        // TEXT VIST 行格式: [ts][#seq][NAME] key=value ...
        // 匹配示例: [520][#2][TCO1] Speed=80.50km/h
        static const QRegularExpression vistRe(
            QStringLiteral("^\\[\\d+\\]\\[#\\d+\\]\\[(\\w+)\\]\\s*(.*)"));
        const QRegularExpressionMatch vm = vistRe.match(line);
        if (vm.hasMatch()) {
            parseVistTextLine(vm.captured(1), vm.captured(2));
            return;
        }

        // 等待 DM1 头部行: [<ts>][#<seq>] [<CTRL>] MIL:x RSL:x AWL:x PL:x
        static const QRegularExpression headerRe(
            QStringLiteral("^\\[\\d+\\]\\[#\\d+\\]\\s+\\[(\\w+)\\]"));
        const QRegularExpressionMatch m = headerRe.match(line);
        if (m.hasMatch()) {
            m_curController = m.captured(1);
            m_curFaults.clear();
            m_inBlock = true;
        }
    } else {
        if (line.startsWith(QStringLiteral("---"))) {
            // 块结束，发射信号
            qDebug() << "[MCU] DM1 block end, controller:" << m_curController
                     << "faults:" << m_curFaults.size();
            emit dm1Received(m_curController, m_curFaults);
            m_inBlock = false;
            m_curController.clear();
            m_curFaults.clear();
        } else {
            // 故障行:   #N SPN:<spn> FMI:<fmi> OC:<oc> <desc>
            static const QRegularExpression faultRe(
                QStringLiteral("SPN:(\\d+)\\s+FMI:(\\d+)\\s+OC:(\\d+)\\s*(.*)"));
            const QRegularExpressionMatch m = faultRe.match(line);
            if (m.hasMatch()) {
                McuFaultInfo fi;
                fi.spn     = m.captured(1).toInt();
                fi.fmi     = m.captured(2).toInt();
                fi.oc      = m.captured(3).toInt();
                fi.rawDesc = m.captured(4).trimmed();
                qDebug() << "[MCU] Fault parsed: SPN" << fi.spn
                         << "FMI" << fi.fmi << "OC" << fi.oc << fi.rawDesc;
                m_curFaults.append(fi);
            }
            // "  No Active Faults" 行忽略（faults 保持空，等 "---" 时发射）
        }
    }
}

void McuSerialReader::parseVistTextLine(const QString &name, const QString &kv)
{
    if (name == QLatin1String("OEL")) {
        // "R-Turn=OFF L-Turn=ON Backup=OFF" 或 turn_sig 形式
        // TEXT 格式示例: [530][#3][LC] R-Turn=OFF L-Turn=ON Backup=OFF
        // OEL TEXT 示例: [690][#19][OEL] R-Turn=OFF L-Turn=ON
        const bool lTurnOn = kv.contains(QLatin1String("L-Turn=ON"), Qt::CaseInsensitive);
        const bool rTurnOn = kv.contains(QLatin1String("R-Turn=ON"), Qt::CaseInsensitive);
        m_canLTurn = lTurnOn ? 1 : 0;
        m_canRTurn = rTurnOn ? 1 : 0;
        qDebug() << "[TXRX TEXT] OEL lTurn=" << m_canLTurn << "rTurn=" << m_canRTurn;
        emit lcReceived(m_canRTurn, m_canLTurn, m_canBackup);
    } else if (name == QLatin1String("LC")) {
        // [530][#3][LC] R-Turn=OFF L-Turn=ON Backup=OFF Marker=ON
        const bool lTurnOn  = kv.contains(QLatin1String("L-Turn=ON"),  Qt::CaseInsensitive);
        const bool rTurnOn  = kv.contains(QLatin1String("R-Turn=ON"),  Qt::CaseInsensitive);
        const bool backupOn = kv.contains(QLatin1String("Backup=ON"),  Qt::CaseInsensitive);
        m_canBackup = backupOn ? 1 : 0;
        if (m_canLTurn == 0 && m_canRTurn == 0) {
            m_canLTurn = lTurnOn ? 1 : 0;
            m_canRTurn = rTurnOn ? 1 : 0;
        }
        qDebug() << "[TXRX TEXT] LC lTurn=" << m_canLTurn
                 << "rTurn=" << m_canRTurn << "backup=" << m_canBackup;
        emit lcReceived(m_canRTurn, m_canLTurn, m_canBackup);
    } else if (name == QLatin1String("TCO1")) {
        // "Speed=80.50km/h"
        static const QRegularExpression re(QStringLiteral("Speed=([\\d.]+)"));
        const QRegularExpressionMatch m = re.match(kv);
        if (m.hasMatch()) {
            const float speed = m.captured(1).toFloat();
            qDebug() << "[TXRX TEXT] TCO1 speed=" << speed;
            emit AppSignals::instance()->vehicleSpeedChanged(speed);
        }
    }
    // TD 时间日期在 TEXT 模式下暂未规定，依赖 JSON 模式处理
}
