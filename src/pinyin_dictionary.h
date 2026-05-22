#ifndef PINYIN_DICTIONARY_H
#define PINYIN_DICTIONARY_H

#include <QString>
#include <QStringList>

class PinyinDictionary {
public:
    static QStringList candidates(const QString &pinyin);
};

#endif // PINYIN_DICTIONARY_H
