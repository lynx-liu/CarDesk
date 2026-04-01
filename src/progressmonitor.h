#ifndef PROGRESSMONITOR_H
#define PROGRESSMONITOR_H

#include <QObject>
#include <QString>
#include <QByteArray>

class QLocalSocket;
class QTimer;

class ProgressMonitor : public QObject {
    Q_OBJECT

public:
    explicit ProgressMonitor(QObject *parent = nullptr);
    ~ProgressMonitor();

    // 启动连接socket并监控进度
    void start();

    // 停止监控
    void stop();

signals:
    void connected();
    void disconnected();
    void progress(int percentage, const QString &state);
    void errorOccurred(const QString &error);

private slots:
    void onSocketConnected();
    void onSocketDisconnected();
    void onSocketError(int error);
    void onSocketReadyRead();
    void onRetryConnection();

private:
    // 尝试连接socket
    bool connectToSocket();

    // 解析swupdate的进度消息（优先按progress_msg二进制协议）
    void parseProgressMessage(const QByteArray &data);

    // 解析swupdate progress_ipc.h 的二进制 progress_msg
    bool parseBinaryProgressFrame(const QByteArray &frame);

    // 解析JSON格式的进度信息
    void parseJsonProgress(const QString &jsonStr);

    // 将状态码映射为文本
    QString statusToText(int status) const;

    QLocalSocket *m_socket;
    QTimer *m_retryTimer;
    int m_currentProgress;
    QString m_lastState;
    bool m_isRunning;
    int m_retryCount;
    QByteArray m_rxBuffer;

    static const int PROGRESS_MSG_SIZE = 2400;
    static const int MAX_RETRY_COUNT = 50;
    static const int RETRY_INTERVAL = 100;  // 毫秒
};

#endif // PROGRESSMONITOR_H
