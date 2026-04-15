#include "appsignals.h"
#include <QProcess>
#include <QTimer>
#include <QCoreApplication>
#include <QDebug>

// AppSignals 是 header-only 单例；此文件提供 runAmixer 实现，
// 用于集中执行系统音量变更并在变更后读取最新数值广播给所有监听者。

void AppSignals::runAmixer(const QStringList &args, QObject *parent)
{
	// Special-case relative +/- commands like "5%+" or "5%-":
	// read current percent, map to 0..10 level, apply delta (+1/-1),
	// then set exact percent = level*10 to avoid uneven quantization.
	if (args.size() >= 3 && args[0] == "sset" && args[1] == "LINEOUT volume") {
		const QString op = args[2];
		if (op.endsWith("%+") || op.endsWith("%-")) {
			const int delta = op.endsWith("%+") ? 1 : -1;
			QProcess *reader = new QProcess(parent ? parent : AppSignals::instance());
			QObject::connect(reader, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), reader,
				[reader, parent, delta]() {
				const QString out = QString::fromLocal8Bit(reader->readAllStandardOutput());
				reader->deleteLater();
				const int lb = out.lastIndexOf('[');
				const int pct = out.indexOf('%', lb);
				int v = 0;
				if (lb >= 0 && pct > lb) {
					bool ok = false;
					v = out.mid(lb + 1, pct - lb - 1).toInt(&ok) ? out.mid(lb + 1, pct - lb - 1).toInt() : 0;
				}
				// Map percent to 0..10 level (nearest)
				int curLevel = qRound(v / 10.0);
				int targetLevel = qBound(0, curLevel + delta, 10);
				int targetPercent = targetLevel * 10;
				QString percentArg = QString::number(targetPercent) + "%";
				QProcess *proc = new QProcess(parent ? parent : AppSignals::instance());
				QObject::connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), proc,
					[proc]() {
					QProcess *reader2 = new QProcess(proc->parent());
					QObject::connect(reader2, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), reader2,
						[reader2]() {
							const QString out2 = QString::fromLocal8Bit(reader2->readAllStandardOutput());
							reader2->deleteLater();
							const int lb2 = out2.lastIndexOf('[');
							const int pct2 = out2.indexOf('%', lb2);
							if (lb2 >= 0 && pct2 > lb2) {
								bool ok = false;
								const int v2 = out2.mid(lb2 + 1, pct2 - lb2 - 1).toInt(&ok);
								if (ok) {
									const int lv = qBound(0, (v2 + 5) / 10, 10);
									QCoreApplication::instance()->setProperty("appVolumeLevel", lv);
									AppSignals::instance()->volumeLevelChanged(lv);
								}
							}
					});
					reader2->start("amixer", {"sget", "LINEOUT volume"});
					proc->deleteLater();
				});
				proc->start("amixer", {"sset", "LINEOUT volume", percentArg});
			});
			reader->start("amixer", {"sget", "LINEOUT volume"});
			return;
		}
	}

	// Default behavior: run requested amixer command and then read back current volume
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
