#include "appsignals.h"
#include <QProcess>
#include <QTimer>
#include <QCoreApplication>
#include <QDebug>

namespace {
static const int kVolumeLevelToDigital[11] = {63, 20, 15, 10, 8, 6, 4, 3, 2, 1, 0};

static int parseDigitalVolume(const QString &out)
{
    const int lb = out.lastIndexOf('[');
    const QString before = (lb >= 0) ? out.left(lb).trimmed() : out.trimmed();
    const int colon = before.lastIndexOf(':');
    const QString token = (colon >= 0) ? before.mid(colon + 1).trimmed() : before;
    bool ok = false;
    const int value = token.toInt(&ok);
    return ok ? value : 0;
}
}

int AppSignals::volumeDigitalFromLevel(int level)
{
    return kVolumeLevelToDigital[qBound(0, level, 10)];
}

int AppSignals::volumeLevelFromDigital(int digitalValue)
{
    const int d = qBound(0, digitalValue, 63);
    int bestLevel = 0;
    int bestDiff = qAbs(d - kVolumeLevelToDigital[0]);
    for (int i = 1; i < 11; ++i) {
        const int diff = qAbs(d - kVolumeLevelToDigital[i]);
        if (diff < bestDiff) {
            bestLevel = i;
            bestDiff = diff;
        }
    }
    return bestLevel;
}

void AppSignals::setVolumeLevel(int level, QObject *parent)
{
    const int digitalValue = AppSignals::volumeDigitalFromLevel(level);
    qDebug() << "[AppSignals] setVolumeLevel:" << level
             << "-> digital volume:" << digitalValue;
    runAmixer({"sset", "digital volume", QString::number(digitalValue)}, parent);
}

void AppSignals::changeVolume(int delta, QObject *parent)
{
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
            const int currentLevel = AppSignals::volumeLevelFromDigital(parseDigitalVolume(out));
            const int targetLevel = qBound(0, currentLevel + delta, 10);
            AppSignals::setVolumeLevel(targetLevel, parent);
    });
    reader->start("amixer", {"sget", "digital volume"});
}

// AppSignals 是 header-only 单例；此文件提供 runAmixer 实现，
// 用于集中执行系统音量变更并在变更后读取最新数值广播给所有监听者。

void AppSignals::runAmixer(const QStringList &args, QObject *parent)
{
	// Special-case relative +/- commands like "5%+" or "5%-":
	// read current digital volume, map to the app volume level, apply delta (+1/-1),
	// and set the target digital value using the fixed volume table.
	if (args.size() >= 3 && args[0] == "sset" && args[1] == "digital volume") {
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
				// Map digital volume to 0..10 level (nearest)
				int curLevel = AppSignals::volumeLevelFromDigital(parseDigitalVolume(out));
				int targetLevel = qBound(0, curLevel + delta, 10);
				int targetValue = AppSignals::volumeDigitalFromLevel(targetLevel);
                qDebug() << "[AppSignals] changeVolume delta:" << delta \
                         << "currentLevel:" << curLevel \
                         << "targetLevel:" << targetLevel \
                         << "targetDigital:" << targetValue;
				QString valueArg = QString::number(targetValue);
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
									const int lv = AppSignals::volumeLevelFromDigital(parseDigitalVolume(out2));
                                    qDebug() << "[AppSignals] readback digital volume:" << parseDigitalVolume(out2) \
                                         << "level:" << lv \
                                         << "raw:" << out2;
									QCoreApplication::instance()->setProperty("appVolumeLevel", lv);
									AppSignals::instance()->volumeLevelChanged(lv);
								}
							}
					});
					reader2->start("amixer", {"sget", "digital volume"});
					proc->deleteLater();
				});
				proc->start("amixer", {"sset", "digital volume", valueArg});
			});
			reader->start("amixer", {"sget", "digital volume"});
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
					const int lv = AppSignals::volumeLevelFromDigital(parseDigitalVolume(out));
					qDebug() << "[AppSignals] readback digital volume:" << parseDigitalVolume(out)
					         << "level:" << lv
					         << "raw:" << out;
					QCoreApplication::instance()->setProperty("appVolumeLevel", lv);
					AppSignals::instance()->volumeLevelChanged(lv);
				}
			}
		});
		reader->start("amixer", {"sget", "digital volume"});
		proc->deleteLater();
	});
	proc->start("amixer", args);
}
