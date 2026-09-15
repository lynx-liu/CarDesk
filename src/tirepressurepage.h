#ifndef TIREPRESSUREPAGE_H
#define TIREPRESSUREPAGE_H

#include "mcuserialreader.h"

#include <QHash>
#include <QWidget>

class QLabel;
class QTimer;

/** 系统设置「胎压显示」页：牵引头 + 挂车布局，接收 MCU TPMS 实时刷新 */
class TirePressurePage : public QWidget {
    Q_OBJECT

public:
    explicit TirePressurePage(QWidget *parent = nullptr);

protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    struct TireCell {
        QLabel *pressure = nullptr;
        QLabel *temperature = nullptr;
        int axle = -1;
        int tire = -1;
    };

    struct TireSlot {
        McuTpmsInfo info;
        qint64 lastRxMs = 0;
        bool valid = false;
    };

    void setupUi();
    QWidget *makeColumn(QWidget *parent, int marginLeft, int marginRight);
    void addUnitCell(QWidget *column, const QString &text, bool topPad, int afterGap);
    TireCell *addDataCell(QWidget *column, int axle, int tire, int afterGap);
    void onTpmsReceived(const McuTpmsInfo &info);
    void refreshStale();
    void applyCell(TireCell *cell, const TireSlot *slot, qint64 nowMs) const;
    static QString pressureText(int kpa);
    static QColor pressureColor(const QString &alarm, bool stale);
    static void stylePressureLabel(QLabel *label, const QColor &color);
    static void styleTemperatureLabel(QLabel *label, const QColor &color);

    QHash<quint32, TireSlot> m_slots; // key = axle<<8 | tire
    QVector<TireCell *> m_cells;
    QTimer *m_staleTimer = nullptr;
};

#endif // TIREPRESSUREPAGE_H
