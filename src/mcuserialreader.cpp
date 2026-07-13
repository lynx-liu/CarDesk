#include "mcuserialreader.h"
#include "appsignals.h"
#include "automotivedriving.h"

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

McuSerialReader *McuSerialReader::existingShared()
{
    return s_shared;
}

void McuSerialReader::clearCanSignalState()
{
    m_canRTurn = 0;
    m_canLTurn = 0;
    m_canBackup = 0;
    m_lastEmittedRTurn = -1;
    m_lastEmittedLTurn = -1;
    m_lastEmittedBackup = -1;
    qDebug() << "[TXRX] clear CAN signal state after layout release";
    automotiveSyncCanSignals(0, 0, 0);
}

McuSerialReader::McuSerialReader(QObject *parent)
    : QObject(parent)
    , m_port(new QSerialPort(this))
    , m_upgradeMode(false)
    , m_inBlock(false)
    , m_canRTurn(0)
    , m_canLTurn(0)
    , m_canBackup(0)
    , m_oelReceived(false)
{
    connect(m_port, &QSerialPort::readyRead, this, &McuSerialReader::onReadyRead);
}

McuSerialReader::~McuSerialReader()
{
    close();
}

bool McuSerialReader::open(const QString &portName)
{
    if (m_port->isOpen())
        return true;   // 已打开，幂等返回成功
    m_port->setPortName(portName);
    m_port->setBaudRate(QSerialPort::Baud115200);
    m_port->setDataBits(QSerialPort::Data8);
    m_port->setParity(QSerialPort::NoParity);
    m_port->setStopBits(QSerialPort::OneStop);
    m_port->setFlowControl(QSerialPort::NoFlowControl);
    const bool ok = m_port->open(QIODevice::ReadWrite);
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

void McuSerialReader::write(const QByteArray &data)
{
    if (!m_port || !m_port->isOpen()) {
        qWarning() << "[MCU] Cannot write to serial port, not open!";
        return;
    }

    // 文本命令可打印；YMODEM 二进制帧只打长度，避免 fromLatin1 遇 \\0 截断/拖垮日志
    const bool looksBinary = data.contains('\0')
            || data.contains(char(0x01)) || data.contains(char(0x02))
            || data.contains(char(0x04));
    if (looksBinary) {
        qDebug() << "[MCU TX] binary bytes=" << data.size()
                 << "head=" << data.left(4).toHex();
    } else {
        qDebug() << "[MCU TX]" << QString::fromLatin1(data).trimmed();
    }

    qint64 offset = 0;
    while (offset < data.size()) {
        const qint64 n = m_port->write(data.constData() + offset, data.size() - offset);
        if (n < 0) {
            qWarning() << "[MCU] serial write error:" << m_port->errorString();
            return;
        }
        if (n == 0) {
            if (!m_port->waitForBytesWritten(1000)) {
                qWarning() << "[MCU] waitForBytesWritten timeout, wrote"
                           << offset << "/" << data.size();
                return;
            }
            continue;
        }
        offset += n;
    }
    if (!m_port->waitForBytesWritten(2000)) {
        qWarning() << "[MCU] flush timeout after writing" << data.size() << "bytes";
    }
}

void McuSerialReader::setUpgradeMode(bool mode)
{
    m_upgradeMode = mode;
    // 进出升级模式都清空解析缓冲，避免 CAN 文本残留混进 YMODEM
    m_buf.clear();
    m_inBlock = false;
    m_curFaults.clear();
    if (mode) {
        discardInput();
    }
    qDebug() << "[MCU] upgradeMode" << (mode ? "ON" : "OFF");
}

void McuSerialReader::discardInput()
{
    if (!m_port || !m_port->isOpen()) {
        return;
    }
    const QByteArray junk = m_port->readAll();
    if (!junk.isEmpty()) {
        qDebug() << "[MCU] discardInput dropped" << junk.size() << "bytes";
    }
}

void McuSerialReader::onReadyRead()
{
    const QByteArray incoming = m_port->readAll();
    if (m_upgradeMode) {
        qDebug() << "[MCU RX]" << QString::fromLatin1(incoming);
        emit rawDataReceived(incoming);
        return;
    }
    m_buf += incoming;
    int pos;
    while ((pos = m_buf.indexOf('\n')) != -1) {
        QByteArray line = m_buf.left(pos);
        m_buf = m_buf.mid(pos + 1);
        // 去掉末尾 \r
        if (!line.isEmpty() && line.back() == '\r')
            line.chop(1);
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
        m_oelReceived = true;
        const int turnSig = obj.value(QStringLiteral("turn_sig")).toInt();
        m_canLTurn = (turnSig == 1) ? 1 : 0;
        m_canRTurn = (turnSig == 2) ? 1 : 0;
        qDebug() << "[TXRX] OEL turn_sig=" << turnSig
                 << "lTurn=" << m_canLTurn << "rTurn=" << m_canRTurn;
        emitLcIfChanged();
    } else if (name == QLatin1String("LC")) {
        // 0x0CFE4121: 只取 backup 字段，转向信号仅由 OEL 提供
        const int backup = obj.value(QStringLiteral("backup")).toInt();
        m_canBackup = backup;
        qDebug() << "[TXRX] LC backup=" << m_canBackup;
        emitLcIfChanged();
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

void McuSerialReader::emitLcIfChanged()
{
    if (m_canRTurn == m_lastEmittedRTurn && m_canLTurn == m_lastEmittedLTurn
        && m_canBackup == m_lastEmittedBackup) {
        automotiveRefreshCanBusActivity();
        return;
    }

    automotiveSyncCanSignals(m_canRTurn, m_canLTurn, m_canBackup);

    m_lastEmittedRTurn = m_canRTurn;
    m_lastEmittedLTurn = m_canLTurn;
    m_lastEmittedBackup = m_canBackup;
    emit lcReceived(m_canRTurn, m_canLTurn, m_canBackup);
}

void McuSerialReader::parseVistTextLine(const QString &name, const QString &kv)
{
    if (name == QLatin1String("OEL")) {
        // 支持两种 TEXT 格式：TurnSignal=N(...) 或 R-Turn/L-Turn
        // 示例: TurnSignal=2(RIGHT)  或  R-Turn=OFF L-Turn=ON
        m_oelReceived = true;
        static const QRegularExpression tsRe(QStringLiteral("TurnSignal=(\\d+)") );
        const QRegularExpressionMatch tsm = tsRe.match(kv);
        if (tsm.hasMatch()) {
            const int sig = tsm.captured(1).toInt();
            m_canLTurn = (sig == 1) ? 1 : 0;
            m_canRTurn = (sig == 2) ? 1 : 0;
        } else {
            const bool lTurnOn = kv.contains(QLatin1String("L-Turn=ON"), Qt::CaseInsensitive);
            const bool rTurnOn = kv.contains(QLatin1String("R-Turn=ON"), Qt::CaseInsensitive);
            m_canLTurn = lTurnOn ? 1 : 0;
            m_canRTurn = rTurnOn ? 1 : 0;
        }
        qDebug() << "[TXRX TEXT] OEL lTurn=" << m_canLTurn << "rTurn=" << m_canRTurn << " raw=" << kv;
        emitLcIfChanged();
    } else if (name == QLatin1String("LC")) {
        // [530][#3][LC] R-Turn=OFF L-Turn=ON Backup=OFF Marker=ON
        // 转向信号仅由 OEL 提供，这里只取 backup
        const bool backupOn = kv.contains(QLatin1String("Backup=ON"),  Qt::CaseInsensitive);
        m_canBackup = backupOn ? 1 : 0;
        qDebug() << "[TXRX TEXT] LC backup=" << m_canBackup << " raw=" << kv;
        emitLcIfChanged();
    } else if (name == QLatin1String("TCO1")) {
        // "Speed=80.50km/h"
        static const QRegularExpression re(QStringLiteral("Speed=([\\d.]+)"));
        const QRegularExpressionMatch m = re.match(kv);
        if (m.hasMatch()) {
            const float speed = m.captured(1).toFloat();
            qDebug() << "[TXRX TEXT] TCO1 speed=" << speed;
            emit AppSignals::instance()->vehicleSpeedChanged(speed);
        }
    } else if (name == QLatin1String("TD_VIST") || name == QLatin1String("TD_OTHER")) {
        // 解析 TEXT 格式时间: 例如 "2025-04-0003 14:53"（容错解析）
        static const QRegularExpression tr(QStringLiteral("(\\d{4})-(\\d{1,2})-(\\d{1,4})\\s+(\\d{1,2}):(\\d{1,2})"));
        const QRegularExpressionMatch tm = tr.match(kv);
        if (tm.hasMatch()) {
            const int year = tm.captured(1).toInt();
            const int month = tm.captured(2).toInt();
            int day = tm.captured(3).toInt();
            const int hour = tm.captured(4).toInt();
            const int minute = tm.captured(5).toInt();
            // 若 day 看起来超出正常范围，尝试截取最低8位
            if (day > 31) day = day % 100;
            qDebug() << "[TXRX TEXT]" << name << year << month << day << hour << minute << " raw=" << kv;
            emit tdReceived(year, month, day, hour, minute);
        } else {
            qDebug() << "[TXRX TEXT]" << name << "parse failed raw=" << kv;
        }
    }
    // TD 时间日期在 TEXT 模式下暂未规定，依赖 JSON 模式处理
}
