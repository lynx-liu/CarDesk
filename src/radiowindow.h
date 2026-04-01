#ifndef RADIOWINDOW_H
#define RADIOWINDOW_H

#include <QMainWindow>

class QLabel;
class QPushButton;
class QListWidget;
class QDialog;

class RadioWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit RadioWindow(QWidget *parent = nullptr);

signals:
    void requestReturnToMain();

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onSwitchFM();
    void onSwitchAM();
    void onPrev();
    void onNext();
    void onToggleFavorite();
    void onToggleScan();
    void onTogglePlay();
    void onSearch();
    void onOpenListDialog();

private:
    void setupUI();
    void updateFrequencyView();
    void setupTopStatusIcons(QWidget *topBar);
    void rebuildStationStrip();
    void switchBand(bool fm);

    QLabel *m_freqLabel;
    QLabel *m_unitLabel;
    QLabel *m_barLabel;
    QLabel *m_scaleLabel;
    QPushButton *m_fmTabBtn;
    QPushButton *m_amTabBtn;
    QPushButton *m_searchBtn;
    QPushButton *m_playBtn;
    QPushButton *m_favoriteBtn;
    QPushButton *m_scanBtn;
    QListWidget *m_stationList;

    QStringList m_fmStations;
    QStringList m_amStations;
    bool m_isFM;
    double m_frequency;
    bool m_favorite;
    bool m_scanMode;
    bool m_playing;
};

#endif // RADIOWINDOW_H
