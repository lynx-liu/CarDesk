#include "applog.h"

#include <QDateTime>
#include <QDebug>
#include <QMutex>
#include <QMutexLocker>
#include <QString>
#include <cstdio>

namespace {

QMutex s_logMutex;

const char *messageTypeLabel(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:
        return "DEBUG";
    case QtInfoMsg:
        return "INFO";
    case QtWarningMsg:
        return "WARN";
    case QtCriticalMsg:
        return "CRIT";
    case QtFatalMsg:
        return "FATAL";
    }
    return "LOG";
}

void appMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    Q_UNUSED(context);

    const QString timestamp =
        QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
    const QByteArray line = QStringLiteral("%1 [%2] %3")
                                .arg(timestamp, QString::fromLatin1(messageTypeLabel(type)), msg)
                                .toLocal8Bit();

    QMutexLocker lock(&s_logMutex);
    FILE *out = (type == QtDebugMsg || type == QtInfoMsg) ? stdout : stderr;
    fprintf(out, "%s\n", line.constData());
    fflush(out);

    if (type == QtFatalMsg) {
        abort();
    }
}

} // namespace

void installAppLogHandler()
{
    static bool installed = false;
    if (installed) {
        return;
    }
    installed = true;
    qInstallMessageHandler(appMessageHandler);
}

void logBuildInfo()
{
#ifdef APP_BUILD_DATETIME
    qDebug().noquote() << QStringLiteral("CarDesk build time:") << QStringLiteral(APP_BUILD_DATETIME);
#endif
}
