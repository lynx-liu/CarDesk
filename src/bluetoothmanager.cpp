#include "bluetoothmanager.h"
#include "t507sdkbridge.h"
#include <QDebug>
#include <QProcess>
#include <QRegularExpression>
#include <QTimer>

BluetoothManager::BluetoothManager(QObject *parent)
    : QObject(parent)
    , m_isConnected(false)
    , m_port(new QSerialPort(this))
    , m_initialized(false)
    , m_scanning(false)
    , m_btEnabled(true)
    , m_queryingPaired(false)
    , m_queryState(BluetoothQueryState::None)
    , m_clearingAddress()
{
    connect(m_port, &QSerialPort::readyRead, this, &BluetoothManager::onSerialReadyRead);
}

BluetoothManager::~BluetoothManager() {
    if (m_port->isOpen())
        m_port->close();
}

void BluetoothManager::scanDevices() {
    qDebug() << "BluetoothManager: scanning for devices...";
    if (!ensureInitialized()) {
        emit error(QStringLiteral("无法初始化蓝牙串口"));
        return;
    }

    if (!m_btEnabled) {
        sendAtCommand(QStringLiteral("P1"));
        m_btEnabled = true;
    }

    if (!m_port->isOpen()) {
        emit error(QStringLiteral("蓝牙串口未打开"));
        return;
    }

    m_scanning = true;
    emit scanStarted();
    sendAtCommand(QStringLiteral("SD"));
}

void BluetoothManager::stopScan() {
    if (!ensureInitialized()) return;
    if (!m_port->isOpen()) return;
    sendAtCommand(QStringLiteral("ST"));
    m_scanning = false;
}

void BluetoothManager::queryPairedDevices() {
    qDebug() << "BluetoothManager: querying paired devices...";
    if (!ensureInitialized()) {
        emit error(QStringLiteral("无法初始化蓝牙串口"));
        return;
    }
    if (!m_port->isOpen()) {
        emit error(QStringLiteral("蓝牙串口未打开"));
        return;
    }
    m_queryingPaired = true;
    sendAtCommand(QStringLiteral("MX"));
}

void BluetoothManager::clearPairedDevice(const QString &address) {
    if (!ensureInitialized()) {
        emit error(QStringLiteral("无法初始化蓝牙串口"));
        return;
    }
    const QString addr = normalizeAddress(address);
    if (addr.isEmpty()) {
        emit error(QStringLiteral("蓝牙地址格式错误"));
        return;
    }
    if (m_isConnected && normalizeAddress(m_connectedDeviceAddress) == addr) {
        disconnectDevice();
    }
    m_clearingAddress = addr;
    sendAtCommand(QStringLiteral("CV[%1]").arg(addr));
}

void BluetoothManager::connectDevice(const QString &deviceAddress) {
    qDebug() << "BluetoothManager: connecting to device:" << deviceAddress;
    if (!ensureInitialized()) {
        emit error(QStringLiteral("无法初始化蓝牙串口"));
        return;
    }

    if (!deviceAddress.isEmpty()) {
        const QString addr = normalizeAddress(deviceAddress);
        if (addr.isEmpty()) {
            emit error(QStringLiteral("蓝牙地址格式错误"));
            return;
        }
        sendAtCommand(QStringLiteral("CC[%1]").arg(addr));
    } else {
        sendAtCommand(QStringLiteral("CC"));
    }
}

void BluetoothManager::disconnectDevice() {
    qDebug() << "BluetoothManager: disconnecting device";
    if (!ensureInitialized()) {
        return;
    }
    sendAtCommand(QStringLiteral("CD"));
    if (m_isConnected) {
        m_isConnected = false;
        m_connectedDeviceAddress.clear();
        m_connectedDeviceName.clear();
        emit deviceDisconnected();
    }
}

bool BluetoothManager::isConnected() const {
    return m_isConnected;
}

QString BluetoothManager::getConnectedDeviceName() const {
    return m_connectedDeviceName;
}

QString BluetoothManager::getConnectedDeviceAddress() const {
    return m_connectedDeviceAddress;
}

bool BluetoothManager::isBluetoothEnabled() const {
    return m_btEnabled;
}

QString BluetoothManager::getBluetoothName() const {
    return m_deviceName;
}

QString BluetoothManager::getBluetoothPin() const {
    return m_pinCode;
}

void BluetoothManager::queryBluetoothSettings() {
    if (!ensureInitialized()) {
        emit error(QStringLiteral("无法初始化蓝牙串口"));
        return;
    }
    if (!m_port->isOpen()) {
        emit error(QStringLiteral("蓝牙串口未打开"));
        return;
    }
    if (m_queryState != BluetoothQueryState::None) {
        qDebug() << "BluetoothManager: query already in progress, skipping duplicate request";
        return;
    }
    m_queryState = BluetoothQueryState::QueryName;
    sendAtCommand(QStringLiteral("MM"));
}

void BluetoothManager::queryConnectedDevice() {
    if (!ensureInitialized()) {
        emit error(QStringLiteral("无法初始化蓝牙串口"));
        return;
    }
    if (!m_port->isOpen()) {
        emit error(QStringLiteral("蓝牙串口未打开"));
        return;
    }
    sendAtCommand(QStringLiteral("QB"));
}

void BluetoothManager::dialNumber(const QString &number) {
    if (number.trimmed().isEmpty()) {
        emit error(QStringLiteral("拨号号码不能为空"));
        return;
    }
    if (!ensureInitialized()) {
        emit error(QStringLiteral("无法初始化蓝牙串口"));
        return;
    }
    const QString digits = number.trimmed().remove(QRegularExpression("[^0-9+*#]"));
    if (digits.isEmpty()) {
        emit error(QStringLiteral("拨号号码不能为空"));
        return;
    }
    sendAtCommand(QStringLiteral("CW[%1]").arg(digits));
}

bool BluetoothManager::sendDtmfDigit(const QString &digit) {
    if (!ensureInitialized()) {
        emit error(QStringLiteral("无法初始化蓝牙串口"));
        return false;
    }
    const QString tone = digit.trimmed().remove(QRegularExpression("[^0-9+*#]"));
    if (tone.isEmpty()) {
        return false;
    }
    return sendAtCommand(QStringLiteral("CX%1").arg(tone));
}

bool BluetoothManager::setCallMute(bool mute) {
    if (!ensureInitialized()) {
        emit error(QStringLiteral("无法初始化蓝牙串口"));
        return false;
    }
    return sendAtCommand(QStringLiteral("CM[%1]").arg(mute ? 1 : 0));
}

void BluetoothManager::answerCall() {
    if (!ensureInitialized()) return;
    sendAtCommand(QStringLiteral("CE"));
}

void BluetoothManager::rejectCall() {
    if (!ensureInitialized()) return;
    sendAtCommand(QStringLiteral("CF"));
}

void BluetoothManager::hangupCall() {
    if (!ensureInitialized()) return;
    sendAtCommand(QStringLiteral("CG"));
}

void BluetoothManager::requestPhonebookDownload() {
    if (!ensureInitialized()) return;
    sendAtCommand(QStringLiteral("PB"));
}

void BluetoothManager::requestCallLogDownload() {
    if (!ensureInitialized()) return;
    sendAtCommand(QStringLiteral("PK"));
}

bool BluetoothManager::playPauseMusic() {
    if (!ensureInitialized()) return false;
    return sendAtCommand(QStringLiteral("MA"));
}

bool BluetoothManager::stopMusic() {
    if (!ensureInitialized()) return false;
    return sendAtCommand(QStringLiteral("MC"));
}

bool BluetoothManager::nextTrack() {
    if (!ensureInitialized()) return false;
    const bool ok = sendAtCommand(QStringLiteral("MD"));
    if (ok) {
        QTimer::singleShot(500, this, &BluetoothManager::queryA2dpTrackInfo);
    }
    return ok;
}

bool BluetoothManager::previousTrack() {
    if (!ensureInitialized()) return false;
    const bool ok = sendAtCommand(QStringLiteral("ME"));
    if (ok) {
        QTimer::singleShot(500, this, &BluetoothManager::queryA2dpTrackInfo);
    }
    return ok;
}

bool BluetoothManager::queryA2dpTrackInfo() {
    if (!ensureInitialized()) return false;
    return sendAtCommand(QStringLiteral("MK"));
}

bool BluetoothManager::connectLastA2dpDevice() {
    if (!ensureInitialized()) {
        emit error(QStringLiteral("无法初始化蓝牙串口"));
        return false;
    }
    return sendAtCommand(QStringLiteral("MI"));
}

bool BluetoothManager::disconnectA2dp() {
    if (!ensureInitialized()) return false;
    return sendAtCommand(QStringLiteral("DA"));
}

void BluetoothManager::setBluetoothOn(bool enabled) {
    if (!ensureInitialized()) {
        emit error(QStringLiteral("无法初始化蓝牙串口"));
        return;
    }
    if (m_btEnabled == enabled) {
        qDebug() << "BluetoothManager: bluetooth already" << (enabled ? "enabled" : "disabled") << ", skipping command";
        return;
    }
    sendAtCommand(enabled ? QStringLiteral("P1") : QStringLiteral("P0"));
    m_btEnabled = enabled;
    emit bluetoothEnabledChanged(enabled);
}

void BluetoothManager::setDeviceName(const QString &name) {
    if (!ensureInitialized()) {
        emit error(QStringLiteral("无法初始化蓝牙串口"));
        return;
    }
    const QString safeName = name.trimmed();
    if (safeName.isEmpty()) {
        emit error(QStringLiteral("设备名称不能为空"));
        return;
    }
    m_deviceName = safeName;
    emit bluetoothNameChanged(m_deviceName);
    sendAtCommand(QStringLiteral("MM[%1]").arg(safeName));
}

void BluetoothManager::setPin(const QString &pin) {
    if (!ensureInitialized()) {
        emit error(QStringLiteral("无法初始化蓝牙串口"));
        return;
    }
    const QString safePin = pin.trimmed();
    if (safePin.isEmpty()) {
        emit error(QStringLiteral("配对码不能为空"));
        return;
    }
    m_pinCode = safePin;
    emit bluetoothPinChanged(m_pinCode);
    sendAtCommand(QStringLiteral("MN[%1]").arg(safePin));
}

void BluetoothManager::onSerialReadyRead() {
    m_buffer += m_port->readAll();
    int pos;
    while ((pos = m_buffer.indexOf('\n')) != -1) {
        QByteArray line = m_buffer.left(pos);
        m_buffer = m_buffer.mid(pos + 1);
        if (!line.isEmpty() && line.endsWith('\r'))
            line.chop(1);
        if (line.isEmpty())
            continue;
        parseLine(line);
    }
}

bool BluetoothManager::ensureInitialized() {
    if (m_initialized)
        return true;

    if (!openSerialPort())
        return false;

    m_initialized = true;
    return true;
}

bool BluetoothManager::openSerialPort() {
    if (m_port->isOpen())
        return true;

    const QString portName = QStringLiteral("/dev/goc_serial");
    m_port->setPortName(portName);
    m_port->setBaudRate(QSerialPort::Baud115200);
    m_port->setDataBits(QSerialPort::Data8);
    m_port->setParity(QSerialPort::NoParity);
    m_port->setStopBits(QSerialPort::OneStop);
    m_port->setFlowControl(QSerialPort::NoFlowControl);

    if (!m_port->open(QIODevice::ReadWrite)) {
        qWarning() << "BluetoothManager: cannot open" << portName << ":" << m_port->errorString();
        return false;
    }

    qDebug() << "BluetoothManager: opened serial port" << portName;
    return true;
}

bool BluetoothManager::sendAtCommand(const QString &command) {
    if (!m_port->isOpen()) {
        qWarning() << "BluetoothManager: serial port not open, cannot send" << command;
        return false;
    }

    const QByteArray payload = QStringLiteral("AT#%1\r\n").arg(command).toLatin1();
    qint64 written = m_port->write(payload);
    if (written != payload.size()) {
        qWarning() << "BluetoothManager: failed to write full command:" << command;
        return false;
    }
    if (!m_port->waitForBytesWritten(200)) {
        qWarning() << "BluetoothManager: timeout writing command" << command;
        return false;
    }
    qDebug() << "BluetoothManager: " << payload.trimmed() << " sent";
    return true;
}

void BluetoothManager::parseLine(const QByteArray &line) {
    const QString text = QString::fromUtf8(line).trimmed();
    qDebug() << "BluetoothManager: received:" << text;

    if (text == "SH") {
        if (m_scanning) {
            m_scanning = false;
            emit scanFinished();
        }
        return;
    }

    if (text.startsWith("SF[")) {
        static const QRegularExpression re(QStringLiteral("^SF\\[([^;]+);(.+)\\]$"));
        const QRegularExpressionMatch match = re.match(line);
        if (match.hasMatch()) {
            const QString addr = normalizeAddress(match.captured(1));
            const QString name = match.captured(2).trimmed();
            if (!addr.isEmpty()) {
                emit deviceFound(name, addr);
            }
        }
        return;
    }

    if (text.startsWith("MX")) {
        static const QRegularExpression reBracket(QStringLiteral("^MX\\d*\\[([^\\]]+)\\]\\[(.*)\\]$"));
        QRegularExpressionMatch match = reBracket.match(line);
        if (match.hasMatch()) {
            const QString addr = normalizeAddress(match.captured(1));
            QString name = match.captured(2).trimmed();
            if (name.isEmpty()) name = addr;
            if (!addr.isEmpty()) {
                emit pairedDeviceFound(name, addr);
            }
            return;
        }
        static const QRegularExpression rePlain(QStringLiteral("^MX\\d*([0-9A-Fa-f]{12})(.*)$"));
        match = rePlain.match(line);
        if (match.hasMatch()) {
            const QString addr = normalizeAddress(match.captured(1));
            QString name = match.captured(2).trimmed();
            if (name.isEmpty()) name = addr;
            if (!addr.isEmpty()) {
                emit pairedDeviceFound(name, addr);
            }
        }
        return;
    }

    if (line.startsWith(QByteArrayLiteral("PB"))) {
        const QByteArray payload = line.mid(2);
        QList<QString> fields;
        const char separator = static_cast<char>(0xFF);
        if (payload.contains(separator)) {
            const QList<QByteArray> rawFields = payload.split(separator);
            for (const QByteArray &field : rawFields) {
                const QString value = QString::fromUtf8(field).trimmed();
                if (!value.isEmpty())
                    fields << value;
            }
        } else {
            const QString payloadText = QString::fromUtf8(payload).trimmed();
            static const QRegularExpression reBracket(QStringLiteral(R"(^PB\[(.*?)\]\[FF\]\[(.*?)\]$)"));
            QRegularExpressionMatch match = reBracket.match(payloadText);
            if (match.hasMatch()) {
                fields << match.captured(1).trimmed();
                fields << match.captured(2).trimmed();
            } else {
                const QRegularExpression reSplit(QStringLiteral(R"((.*?)\s*[\|,;\t]+\s*(.*))"));
                QRegularExpressionMatch splitMatch = reSplit.match(payloadText);
                if (splitMatch.hasMatch()) {
                    fields << splitMatch.captured(1).trimmed();
                    fields << splitMatch.captured(2).trimmed();
                } else {
                    fields << payloadText;
                }
            }
        }
        if (fields.size() >= 2) {
            emit phonebookEntryReceived(fields[0], fields[1]);
        } else {
            qDebug() << "BluetoothManager: PB parse failed, payload=" << payload.toHex() << "text=" << QString::fromUtf8(payload);
        }
        return;
    }

    if (line.startsWith(QByteArrayLiteral("PD")) || line.startsWith(QByteArrayLiteral("PK"))) {
        const QByteArray payload = line.mid(2);
        if (payload.isEmpty()) {
            qDebug() << "BluetoothManager: PD/PK parse failed, empty payload";
            return;
        }

        int type = -1;
        int idx = 0;
        while (idx < payload.size() && payload[idx] >= '0' && payload[idx] <= '9') {
            if (type < 0) type = 0;
            type = type * 10 + (payload[idx] - '0');
            idx++;
        }
        if (type < 0) {
            qDebug() << "BluetoothManager: PD/PK parse failed, missing type, payload=" << payload.toHex();
            return;
        }

        const QByteArray rest = payload.mid(idx);
        const char separator = static_cast<char>(0xFF);
        const QList<QByteArray> rawFields = rest.split(separator);
        QList<QString> fields;
        for (const QByteArray &field : rawFields) {
            fields << QString::fromUtf8(field).trimmed();
        }
        if (!fields.isEmpty() && fields.first().isEmpty() && fields.size() >= 3) {
            fields.removeFirst();
        }
        if (fields.size() >= 2) {
            const QString timeText = fields.size() >= 3 ? fields[2] : QString();
            emit callLogEntryReceived(type, fields[0], fields[1], timeText);
        } else {
            qDebug() << "BluetoothManager: PD/PK parse failed, type=" << type << "fields=" << fields << "payload=" << payload.toHex();
        }
        return;
    }

    if (text == QLatin1String("PC")) {
        emit phonebookDownloadFinished();
        return;
    }

    if (text == QLatin1String("PE")) {
        emit callLogDownloadFinished();
        return;
    }

    if (text.startsWith("MG")) {
        int status = -1;
        static const QRegularExpression reBracket(QStringLiteral(R"(^MG\[(?:index:)?(\d+)\]$)"));
        QRegularExpressionMatch match = reBracket.match(line);
        if (match.hasMatch()) {
            status = match.captured(1).toInt();
        } else {
            const QByteArray payload = line.mid(2).trimmed();
            bool ok = false;
            const int numeric = payload.toInt(&ok);
            if (ok) {
                status = numeric;
            }
        }
        if (status >= 1 && status <= 6) {
            emit callStatusChanged(status);
        }
        return;
    }

    if (text.startsWith("IC") || text.startsWith("ID") || text.startsWith("IG") || text.startsWith("IR") || text.startsWith("IH") || text.startsWith("IN")) {
        const QString source = text.left(2);
        QString number = text.mid(2).trimmed();
        if (number.startsWith(QLatin1Char('[')) && number.endsWith(QLatin1Char(']')) && number.size() >= 2) {
            number = number.mid(1, number.size() - 2);
        }
        if (!number.isEmpty()) {
            emit callNumberUpdated(number, source);
        }
        if (source == QLatin1String("IC")) {
            emit callStatusChanged(4);
        } else if (source == QLatin1String("ID")) {
            emit callStatusChanged(5);
        } else if (source == QLatin1String("IG")) {
            emit callStatusChanged(6);
        } else if (source == QLatin1String("IR") || source == QLatin1String("IH") || source == QLatin1String("IN")) {
            // only update current number
        }
        return;
    }

    if (text == QLatin1String("IF")) {
        emit callStatusChanged(1);
        return;
    }

    if (text.startsWith("CL")) {
        static const QRegularExpression re(QStringLiteral(R"(^CL\[index;?(\d+)\]\[status:(\d+)\]\[(.*?)\]$)"));
        QRegularExpressionMatch match = re.match(line);
        if (match.hasMatch()) {
            const QString number = match.captured(3).trimmed();
            if (!number.isEmpty()) {
                emit callNumberUpdated(number, QStringLiteral("CL"));
            }
            return;
        }

        static const QRegularExpression rePlain(QStringLiteral(R"(^CL(\d+)(\d)(.*)$)"));
        match = rePlain.match(text);
        if (match.hasMatch()) {
            const QString number = match.captured(3).trimmed();
            if (!number.isEmpty()) {
                emit callNumberUpdated(number, QStringLiteral("CL"));
            }
            return;
        }
        return;
    }

    if (text.startsWith("P")) {
        const QString payload = text.mid(1).trimmed();
        if (payload == QLatin1String("1") || payload == QLatin1String("0")) {
            const bool enabled = (payload == QLatin1String("1"));
            if (m_btEnabled != enabled) {
                m_btEnabled = enabled;
                emit bluetoothEnabledChanged(enabled);
            }
            qDebug() << "BluetoothManager: parsed Bluetooth status:" << (enabled ? "on" : "off");
            if (m_queryState == BluetoothQueryState::QueryStatus) {
                m_queryState = BluetoothQueryState::None;
                if (enabled) {
                    queryBluetoothSettings();
                }
            }
            return;
        }
    }

    if (text.startsWith("MM")) {
        QString payload = text.mid(2).trimmed();
        if (payload.startsWith(QLatin1Char('[')) && payload.endsWith(QLatin1Char(']')) && payload.size() >= 2) {
            payload = payload.mid(1, payload.size() - 2).trimmed();
        }
        m_deviceName = payload;
        emit bluetoothNameChanged(m_deviceName);
        qDebug() << "BluetoothManager: parsed device name:" << m_deviceName;
        if (m_queryState == BluetoothQueryState::QueryName) {
            m_queryState = BluetoothQueryState::QueryPin;
            sendAtCommand(QStringLiteral("MN"));
        } else {
            m_queryState = BluetoothQueryState::None;
        }
        return;
    }

    if (text.startsWith("MN")) {
        QString payload = text.mid(2).trimmed();
        if (payload.startsWith(QLatin1Char('[')) && payload.endsWith(QLatin1Char(']')) && payload.size() >= 2) {
            payload = payload.mid(1, payload.size() - 2).trimmed();
        }
        m_pinCode = payload;
        emit bluetoothPinChanged(m_pinCode);
        qDebug() << "BluetoothManager: parsed Bluetooth PIN:" << m_pinCode;
        if (m_queryState == BluetoothQueryState::QueryPin) {
            m_queryState = BluetoothQueryState::None;
        }
        return;
    }

    if (text == QLatin1String("MB")) {
        if (!m_a2dpPlaying) {
            m_a2dpPlaying = true;
            emit a2dpPlaybackStateChanged(true);
        }
        return;
    }

    if (text == QLatin1String("MA")) {
        if (m_a2dpPlaying) {
            m_a2dpPlaying = false;
            emit a2dpPlaybackStateChanged(false);
        }
        return;
    }

    if (line.startsWith(QByteArrayLiteral("MI")) || text.startsWith("MI")) {
        const QByteArray payload = line.mid(2);
        QStringList fields;
        const char separator = static_cast<char>(0xFF);
        if (payload.contains(separator)) {
            const QList<QByteArray> rawFields = payload.split(separator);
            for (const QByteArray &field : rawFields) {
                if (field.isEmpty())
                    continue;
                fields << QString::fromUtf8(field).trimmed();
            }
        } else {
            const QString payloadText = QString::fromUtf8(payload).trimmed();
            QRegularExpression re(QStringLiteral("\\[([^\\]]*)\\]"));
            QRegularExpressionMatchIterator it = re.globalMatch(payloadText);
            while (it.hasNext()) {
                fields << it.next().captured(1);
            }
        }
        if (fields.size() >= 6) {
            const QString title = fields[0].trimmed();
            const QString artist = fields[1].trimmed();
            const QString album = fields[2].trimmed();
            const QString time = fields[3].trimmed();
            const int index = fields[4].toInt();
            const int total = fields[5].toInt();
            const qint64 durationMs = time.toLongLong();
            if (durationMs > 0) {
                // MI time field is typically reported in milliseconds.
                m_a2dpDurationMs = (durationMs >= 1000) ? durationMs : durationMs * 1000;
            }
            if (!m_isConnected) {
                m_isConnected = true;
                if (m_connectedDeviceName.isEmpty()) {
                    m_connectedDeviceName = QStringLiteral("蓝牙音乐");
                }
                emit deviceConnected(m_connectedDeviceName);
            }
            qDebug() << "BluetoothManager: parsed MI track info:" << title << artist << album << time << index << total << "durationMs=" << m_a2dpDurationMs;
            emit a2dpTrackInfoChanged(title, artist, album, time, index, total);
        } else {
            qDebug() << "BluetoothManager: MI parse failed, fields=" << fields.size() << "payload=" << payload.toHex();
        }
        return;
    }

    if (line.startsWith(QByteArrayLiteral("MP")) || text.startsWith("MP")) {
        const QByteArray payload = line.mid(2);
        bool handled = false;
        if (payload.size() >= 6) {
            bool ok1 = false;
            const QString posHex = QString::fromLatin1(payload.mid(0, 6));
            const qint64 posMs = posHex.toLongLong(&ok1, 16);
            if (ok1) {
                qint64 totalMs = m_a2dpDurationMs;
                if (totalMs <= 0 && payload.size() >= 12) {
                    bool ok2 = false;
                    const QString lenHex = QString::fromLatin1(payload.mid(6, 6));
                    const qint64 lenMs = lenHex.toLongLong(&ok2, 16);
                    if (ok2) {
                        totalMs = lenMs;
                    }
                }
                if (totalMs > 0) {
                    qDebug() << "BluetoothManager: parsed MP progress:" << posMs << "totalMs=" << totalMs;
                    emit a2dpProgressChanged(posMs, totalMs);
                    handled = true;
                } else {
                    qDebug() << "BluetoothManager: parsed MP pos only, no total yet:" << posMs;
                    emit a2dpProgressChanged(posMs, 0);
                    handled = true;
                }
            }
        }
        if (!handled) {
            const QString lineText = QString::fromUtf8(line).trimmed();
            static const QRegularExpression re(QStringLiteral("^MP\\[(?:pos:)?(\\d+)\\]\\[(?:length:)?(\\d+)\\]$"));
            const QRegularExpressionMatch match = re.match(lineText);
            if (match.hasMatch()) {
                qint64 posMs = match.captured(1).toLongLong();
                qint64 durMs = match.captured(2).toLongLong();
                if (durMs > 0 && durMs < 1000) {
                    posMs *= 1000;
                    durMs *= 1000;
                }
                qDebug() << "BluetoothManager: parsed MP progress text:" << posMs << durMs;
                emit a2dpProgressChanged(posMs, durMs);
                return;
            }
            qDebug() << "BluetoothManager: MP parse failed payload=" << payload.toHex();
        }
        return;
    }

    if (text == QLatin1String("OK")) {
        if (m_queryingPaired) {
            m_queryingPaired = false;
            emit pairedQueryFinished();
            return;
        }
        if (!m_clearingAddress.isEmpty()) {
            emit pairedDeviceCleared(m_clearingAddress);
            m_clearingAddress.clear();
            return;
        }
        return;
    }

    if (text == QLatin1String("ER")) {
        if (m_queryingPaired) {
            m_queryingPaired = false;
            emit pairedQueryFinished();
        }
        emit error(QStringLiteral("蓝牙模块返回 ER"));
        return;
    }

    if (text.startsWith("IB") || text.startsWith("JH")) {
        static const QRegularExpression reBracket(QStringLiteral("^[A-Z]{2}\\[([^\\]]+)\\]$"));
        QRegularExpressionMatch match = reBracket.match(line);
        if (!match.hasMatch()) {
            static const QRegularExpression reNoBracket(QStringLiteral("^[A-Z]{2}([0-9A-Fa-f]{12})$"));
            match = reNoBracket.match(line);
        }
        if (match.hasMatch()) {
            m_connectedDeviceAddress = normalizeAddress(match.captured(1));
            m_isConnected = true;
            if (m_connectedDeviceName.isEmpty())
                m_connectedDeviceName = m_connectedDeviceAddress;
            emit deviceConnected(m_connectedDeviceName);
        }
        return;
    }

    if (text.startsWith("SA")) {
        static const QRegularExpression reBracket(QStringLiteral("^[A-Z]{2}\\[(.*)\\]$"));
        QRegularExpressionMatch match = reBracket.match(line);
        if (!match.hasMatch()) {
            static const QRegularExpression reNoBracket(QStringLiteral("^SA(.+)$"));
            match = reNoBracket.match(line);
        }
        if (match.hasMatch()) {
            m_connectedDeviceName = match.captured(1).trimmed();
            if (m_connectedDeviceName.isEmpty())
                m_connectedDeviceName = m_connectedDeviceAddress;
            m_isConnected = true;
            emit deviceConnected(m_connectedDeviceName);
        }
        return;
    }

    if (text == QLatin1String("MY")) {
        if (m_isConnected) {
            m_isConnected = false;
            emit deviceDisconnected();
        }
        return;
    }
}

QString BluetoothManager::normalizeAddress(const QString &addr) const {
    QString result = addr;
    result.remove(QRegularExpression("[^0-9A-Fa-f]"));
    return result.toUpper();
}
