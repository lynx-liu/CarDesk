#include "progressmonitor.h"

#include <QLocalSocket>
#include <QTimer>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QtEndian>

ProgressMonitor::ProgressMonitor(QObject *parent)
    : QObject(parent)
    , m_socket(nullptr)
    , m_retryTimer(nullptr)
    , m_currentProgress(0)
    , m_isRunning(false)
    , m_retryCount(0)
{
}

ProgressMonitor::~ProgressMonitor()
{
    stop();
    if (m_socket) {
        delete m_socket;
    }
    if (m_retryTimer) {
        delete m_retryTimer;
    }
}

void ProgressMonitor::start()
{
    if (m_isRunning) {
        qWarning() << "ProgressMonitor is already running";
        return;
    }

    m_isRunning = true;
    m_retryCount = 0;
    m_currentProgress = 0;

    // 尝试连接socket
    if (!connectToSocket()) {
        // 连接失败，启动重试定时器
        if (!m_retryTimer) {
            m_retryTimer = new QTimer(this);
            connect(m_retryTimer, &QTimer::timeout, this, &ProgressMonitor::onRetryConnection);
        }
        m_retryTimer->start(RETRY_INTERVAL);
        qDebug() << "ProgressMonitor: waiting for swupdate socket...";
    }
}

void ProgressMonitor::stop()
{
    m_isRunning = false;

    if (m_retryTimer) {
        m_retryTimer->stop();
    }

    if (m_socket && m_socket->isOpen()) {
        m_socket->disconnectFromServer();
    }
}

bool ProgressMonitor::connectToSocket()
{
    if (!m_socket) {
        m_socket = new QLocalSocket(this);
        connect(m_socket, &QLocalSocket::connected, this, &ProgressMonitor::onSocketConnected);
        connect(m_socket, &QLocalSocket::disconnected, this, &ProgressMonitor::onSocketDisconnected);
        connect(m_socket, QOverload<QLocalSocket::LocalSocketError>::of(&QLocalSocket::error),
                this, &ProgressMonitor::onSocketError);
        connect(m_socket, &QLocalSocket::readyRead, this, &ProgressMonitor::onSocketReadyRead);
    }

    // 尝试连接到swupdate socket
    m_socket->connectToServer("/tmp/swupdateprog");
    if (m_socket->waitForConnected(500)) {
        qDebug() << "ProgressMonitor: connected to swupdate socket";
        return true;
    }

    qDebug() << "ProgressMonitor: failed to connect to socket:" << m_socket->errorString();
    return false;
}

void ProgressMonitor::onSocketConnected()
{
    qDebug() << "ProgressMonitor: socket connected";
    m_retryCount = 0;
    if (m_retryTimer) {
        m_retryTimer->stop();
    }
    emit connected();
}

void ProgressMonitor::onSocketDisconnected()
{
    qDebug() << "ProgressMonitor: socket disconnected";
    emit disconnected();

    // 如果还在运行，尝试重新连接
    if (m_isRunning && m_retryCount < MAX_RETRY_COUNT) {
        if (!m_retryTimer) {
            m_retryTimer = new QTimer(this);
            connect(m_retryTimer, &QTimer::timeout, this, &ProgressMonitor::onRetryConnection);
        }
        m_retryTimer->start(RETRY_INTERVAL);
    }
}

void ProgressMonitor::onSocketError(int error)
{
    qDebug() << "ProgressMonitor: socket error" << error << ":" << m_socket->errorString();
}

void ProgressMonitor::onSocketReadyRead()
{
    if (!m_socket) {
        return;
    }

    // 读取来自socket的数据（SOCK_STREAM，可能拆包/粘包）
    QByteArray data = m_socket->readAll();
    if (data.isEmpty()) {
        return;
    }

    m_rxBuffer.append(data);

    // swupdate progress_thread.c 按固定长度 sizeof(progress_msg) 发送
    while (m_rxBuffer.size() >= PROGRESS_MSG_SIZE) {
        const QByteArray frame = m_rxBuffer.left(PROGRESS_MSG_SIZE);
        m_rxBuffer.remove(0, PROGRESS_MSG_SIZE);
        parseProgressMessage(frame);
    }
}

void ProgressMonitor::onRetryConnection()
{
    if (!m_isRunning) {
        if (m_retryTimer) {
            m_retryTimer->stop();
        }
        return;
    }

    m_retryCount++;
    if (m_retryCount >= MAX_RETRY_COUNT) {
        qWarning() << "ProgressMonitor: max retry count reached, giving up";
        if (m_retryTimer) {
            m_retryTimer->stop();
        }
        emit errorOccurred(QStringLiteral("无法连接到升级服务"));
        return;
    }

    if (connectToSocket()) {
        if (m_retryTimer) {
            m_retryTimer->stop();
        }
    }
}

void ProgressMonitor::parseProgressMessage(const QByteArray &data)
{
    // 1) 优先按照SDK定义的 progress_msg 二进制协议解析
    if (data.size() == PROGRESS_MSG_SIZE && parseBinaryProgressFrame(data)) {
        return;
    }

    // 2) 兼容：JSON / 文本（某些定制swupdate可能输出此格式）
    QString jsonStr = QString::fromUtf8(data);
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isObject()) {
        parseJsonProgress(jsonStr);
        return;
    }

    if (jsonStr.contains("progress", Qt::CaseInsensitive) ||
        jsonStr.contains("%", Qt::CaseSensitive)) {
        QRegExp rxPercent("(\\d+)%");
        if (rxPercent.indexIn(jsonStr) != -1) {
            int progressValue = rxPercent.cap(1).toInt();
            progressValue = qBound(0, progressValue, 100);
            if (progressValue != m_currentProgress) {
                m_currentProgress = progressValue;
                emit progress(m_currentProgress, jsonStr.trimmed());
            }
        }
    }
}

bool ProgressMonitor::parseBinaryProgressFrame(const QByteArray &frame)
{
    if (frame.size() != PROGRESS_MSG_SIZE) {
        return false;
    }

    const uchar *p = reinterpret_cast<const uchar *>(frame.constData());

    const quint32 magic = qFromLittleEndian<quint32>(p + 0);
    const int status = static_cast<int>(qFromLittleEndian<quint32>(p + 4));
    const quint32 dwlPercent = qFromLittleEndian<quint32>(p + 8);
    const quint32 nsteps = qFromLittleEndian<quint32>(p + 12);
    const quint32 curStep = qFromLittleEndian<quint32>(p + 16);
    const quint32 curPercent = qFromLittleEndian<quint32>(p + 20);

    const QByteArray imageRaw(reinterpret_cast<const char *>(p + 24), 256);
    const int imageEnd = imageRaw.indexOf('\0');
    const QString curImage = QString::fromLocal8Bit(imageRaw.left(imageEnd < 0 ? imageRaw.size() : imageEnd));

    const quint32 source = qFromLittleEndian<quint32>(p + 344);
    quint32 infoLen = qFromLittleEndian<quint32>(p + 348);
    if (infoLen > 2048) {
        infoLen = 2048;
    }
    const QString info = QString::fromLocal8Bit(reinterpret_cast<const char *>(p + 352), static_cast<int>(infoLen)).trimmed();

    const int progressValue = qBound(0, static_cast<int>(curPercent), 100);
    QString state = statusToText(status);
    if (state.isEmpty()) {
        state = QStringLiteral("状态:%1").arg(status);
    }

    if (!curImage.isEmpty() && (status == 2 || status == 5)) {
        state += QStringLiteral(" (%1/%2 %3)").arg(curStep).arg(nsteps).arg(curImage);
    }
    if (!info.isEmpty()) {
        state += QStringLiteral(" - %1").arg(info);
    }

    if (progressValue != m_currentProgress || state != m_lastState) {
        m_currentProgress = progressValue;
        m_lastState = state;
        qDebug() << "ProgressMonitor(binary): magic=0x" + QString::number(magic, 16)
                 << "status=" << status
                 << "src=" << source
                 << "dwl=" << dwlPercent
                 << "step=" << curStep << "/" << nsteps
                 << "cur=" << curPercent;
        emit progress(progressValue, state);
    }

    return true;
}

QString ProgressMonitor::statusToText(int status) const
{
    // swupdate_status.h:
    // 0 IDLE, 1 START, 2 RUN, 3 SUCCESS, 4 FAILURE, 5 DOWNLOAD, 6 DONE, 7 SUBPROCESS
    switch (status) {
    case 0:
        return QStringLiteral("空闲");
    case 1:
        return QStringLiteral("开始升级");
    case 2:
        return QStringLiteral("正在安装");
    case 3:
        return QStringLiteral("升级成功");
    case 4:
        return QStringLiteral("升级失败");
    case 5:
        return QStringLiteral("正在下载");
    case 6:
        return QStringLiteral("升级完成");
    case 7:
        return QStringLiteral("子进程处理中");
    default:
        return QString();
    }
}

void ProgressMonitor::parseJsonProgress(const QString &jsonStr)
{
    QJsonDocument doc = QJsonDocument::fromJson(jsonStr.toUtf8());
    if (!doc.isObject()) {
        qWarning() << "ProgressMonitor: failed to parse JSON:" << jsonStr;
        return;
    }

    QJsonObject obj = doc.object();

    // 提取进度百分比
    // swupdate可能使用 "percent" 或 "percentage" 或 "progress"
    int progressValue = m_currentProgress;
    if (obj.contains("percent")) {
        progressValue = obj.value("percent").toInt();
    } else if (obj.contains("percentage")) {
        progressValue = obj.value("percentage").toInt();
    } else if (obj.contains("progress")) {
        progressValue = obj.value("progress").toInt();
    }

    progressValue = qBound(0, progressValue, 100);

    // 提取状态文本
    QString state = m_lastState;
    if (obj.contains("state")) {
        state = obj.value("state").toString();
    } else if (obj.contains("status")) {
        state = obj.value("status").toString();
    } else if (obj.contains("message")) {
        state = obj.value("message").toString();
    }

    // 翻译状态文本
    if (state.contains("running", Qt::CaseInsensitive)) {
        state = QStringLiteral("正在升级...");
    } else if (state.contains("completed", Qt::CaseInsensitive) || 
               state.contains("success", Qt::CaseInsensitive)) {
        state = QStringLiteral("升级成功");
    } else if (state.contains("error", Qt::CaseInsensitive) || 
               state.contains("failed", Qt::CaseInsensitive)) {
        state = QStringLiteral("升级失败");
    } else if (state.contains("waiting", Qt::CaseInsensitive)) {
        state = QStringLiteral("等待中...");
    }

    // 仅在进度或状态改变时发送信号
    if (progressValue != m_currentProgress || state != m_lastState) {
        m_currentProgress = progressValue;
        m_lastState = state;
        qDebug() << "ProgressMonitor: progress" << progressValue << "%" << state;
        emit progress(progressValue, state);
    }
}
