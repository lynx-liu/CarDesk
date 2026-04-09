#include "mcuserialreader.h"

#include <QDebug>
#include <QRegularExpression>
#include <QSerialPort>

McuSerialReader::McuSerialReader(QObject *parent)
    : QObject(parent)
    , m_port(new QSerialPort(this))
    , m_inBlock(false)
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

void McuSerialReader::processLine(const QByteArray &raw)
{
    // MCU 输出均为 ASCII，从 Latin-1 解码即可
    const QString line = QString::fromLatin1(raw);

    if (!m_inBlock) {
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
