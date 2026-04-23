#include "bluetoothmanager.h"
#include "t507sdkbridge.h"
#include <QDebug>
#include <QProcess>
#include <QRegularExpression>

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
    sendAtCommand(QStringLiteral("CW[%1]").arg(digits));
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
    sendAtCommand(QStringLiteral("PA[status:1]"));
}

void BluetoothManager::requestCallLogDownload() {
    if (!ensureInitialized()) return;
    sendAtCommand(QStringLiteral("PA[status:1]"));
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
    return sendAtCommand(QStringLiteral("MD"));
}

bool BluetoothManager::previousTrack() {
    if (!ensureInitialized()) return false;
    return sendAtCommand(QStringLiteral("ME"));
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
        const QString text = QString::fromUtf8(line).trimmed();
        parseLine(text);
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

void BluetoothManager::parseLine(const QString &line) {
    qDebug() << "BluetoothManager: received:" << line;

    if (line == QLatin1String("SH")) {
        if (m_scanning) {
            m_scanning = false;
            emit scanFinished();
        }
        return;
    }

    if (line.startsWith(QStringLiteral("SF["))) {
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

    if (line.startsWith(QStringLiteral("MX"))) {
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

    if (line.startsWith(QStringLiteral("PB"))) {
        static const QRegularExpression re(QStringLiteral("^PB\[(.*?)\]\[FF\]\[(.*?)\]$"));
        QRegularExpressionMatch match = re.match(line);
        if (match.hasMatch()) {
            emit phonebookEntryReceived(match.captured(1).trimmed(), match.captured(2).trimmed());
            return;
        }
        return;
    }

    if (line.startsWith(QStringLiteral("PD"))) {
        static const QRegularExpression re(QStringLiteral("^PD\[type:(\d+)\]\[FF\]\[(.*?)\]\[FF\]\[(.*?)\]\[FF\]?$"));
        QRegularExpressionMatch match = re.match(line);
        if (match.hasMatch()) {
            emit callLogEntryReceived(match.captured(1).toInt(), match.captured(2).trimmed(), match.captured(3).trimmed());
            return;
        }
        return;
    }

    if (line == QLatin1String("PC")) {
        emit phonebookDownloadFinished();
        return;
    }

    if (line == QLatin1String("PE")) {
        emit callLogDownloadFinished();
        return;
    }

    if (line.startsWith(QStringLiteral("MG"))) {
        static const QRegularExpression re(QStringLiteral("^MG\[index:(\d+)\]$"));
        const QRegularExpressionMatch match = re.match(line);
        if (match.hasMatch()) {
            const int status = match.captured(1).toInt();
            emit callStatusChanged(status);
        }
        return;
    }

    if (line.startsWith(QStringLiteral("IR")) || line.startsWith(QStringLiteral("IH")) || line.startsWith(QStringLiteral("IN"))) {
        const QString source = line.left(2);
        QString number = line.mid(2).trimmed();
        if (number.startsWith(QLatin1Char('[')) && number.endsWith(QLatin1Char(']')) && number.size() >= 2) {
            number = number.mid(1, number.size() - 2);
        }
        emit callNumberUpdated(number, source);
        return;
    }

    if (line.startsWith(QStringLiteral("CL"))) {
        static const QRegularExpression re(QStringLiteral("^CL\[index;?(\d+)\]\[status:(\d+)\]\[(.*?)\]$"));
        const QRegularExpressionMatch match = re.match(line);
        if (match.hasMatch()) {
            const int status = match.captured(2).toInt();
            const QString number = match.captured(3).trimmed();
            emit callStatusChanged(status);
            emit callNumberUpdated(number, QStringLiteral("CL"));
        }
        return;
    }

    if (line.startsWith(QStringLiteral("P"))) {
        const QString payload = line.mid(1).trimmed();
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

    if (line.startsWith(QStringLiteral("MM"))) {
        QString payload = line.mid(2).trimmed();
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

    if (line.startsWith(QStringLiteral("MN"))) {
        QString payload = line.mid(2).trimmed();
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

    if (line == QLatin1String("OK")) {
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

    if (line == QLatin1String("ER")) {
        if (m_queryingPaired) {
            m_queryingPaired = false;
            emit pairedQueryFinished();
        }
        emit error(QStringLiteral("蓝牙模块返回 ER"));
        return;
    }

    if (line.startsWith(QStringLiteral("IB")) || line.startsWith(QStringLiteral("JH"))) {
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

    if (line.startsWith(QStringLiteral("SA"))) {
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

    if (line == QLatin1String("MY")) {
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
