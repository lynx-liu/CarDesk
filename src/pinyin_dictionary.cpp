#include "pinyin_dictionary.h"
#include <QFile>
#include <QTextStream>
#include <QHash>
#include <QChar>
#include <limits>
#include <algorithm>

static QHash<QString, QStringList> g_pinyinMap;


static void loadPinyinMap()
{
    if (!g_pinyinMap.isEmpty()) {
        return;
    }

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
            if (!c.isNull()) {
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

    QStringList result = g_pinyinMap.value(normalized);
    // 直接返回拼音表顺序
    return result;
}
