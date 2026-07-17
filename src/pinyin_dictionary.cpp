#include "pinyin_dictionary.h"
#include <QFile>
#include <QTextStream>
#include <QHash>

static QHash<QString, QStringList> g_pinyinMap;

static void loadPinyinMap()
{
    if (!g_pinyinMap.isEmpty()) {
        return;
    }

    // 词库按字频降序（源自 Android PinyinIME / RIME pinyin_simp）
    QFile file(QStringLiteral(":/pinyin/pinyin_table.txt"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }

    QTextStream in(&file);
    in.setCodec("UTF-8");
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) {
            continue;
        }
        const int sep = line.indexOf(QLatin1Char('='));
        if (sep <= 0) {
            continue;
        }
        const QString key = line.left(sep).trimmed().toLower();
        const QString chars = line.mid(sep + 1);
        if (key.isEmpty() || chars.isEmpty()) {
            continue;
        }

        QStringList list;
        list.reserve(chars.size());
        for (const QChar c : chars) {
            if (!c.isNull() && !c.isSpace()) {
                list.append(QString(c));
            }
        }

        g_pinyinMap.insert(key, std::move(list));
    }
}

QStringList PinyinDictionary::candidates(const QString &pinyin)
{
    loadPinyinMap();
    QString normalized = pinyin.trimmed().toLower();
    for (int i = normalized.size() - 1; i >= 0; --i) {
        if (normalized.at(i).isDigit()) {
            normalized.remove(i, 1);
        }
    }

    // 表内已按常用度排序，例如 ta → 他她它塔…
    return g_pinyinMap.value(normalized);
}
