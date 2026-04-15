#include "appsignals.h"
#include <QProcess>
#include <QTimer>
#include <QCoreApplication>
#include <QDebug>

// AppSignals 是 header-only 单例；此文件提供 runAmixer 实现，
// 用于集中执行系统音量变更并在变更后读取最新数值广播给所有监听者。

void AppSignals::runAmixer(const QStringList &args, QObject *parent)
{
	QProcess *proc = new QProcess(parent ? parent : AppSignals::instance());
	QObject::connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), proc,
					 [proc]() {
		QProcess *reader = new QProcess(proc->parent());
		QObject::connect(reader, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), reader,
						 [reader]() {
			const QString out = QString::fromLocal8Bit(reader->readAllStandardOutput());
			reader->deleteLater();
			const int lb = out.lastIndexOf('[');
			const int pct = out.indexOf('%', lb);
			if (lb >= 0 && pct > lb) {
				bool ok = false;
				const int v = out.mid(lb + 1, pct - lb - 1).toInt(&ok);
				if (ok) {
					const int lv = qBound(0, (v + 5) / 10, 10);
					QCoreApplication::instance()->setProperty("appVolumeLevel", lv);
					AppSignals::instance()->volumeLevelChanged(lv);
				}
			}
		});
		reader->start("amixer", {"sget", "LINEOUT volume"});
		proc->deleteLater();
	});
	proc->start("amixer", args);
}
