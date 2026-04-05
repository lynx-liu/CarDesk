#ifndef RADIOWINDOW_H
#define RADIOWINDOW_H

#include <QMainWindow>
#include <QTimer>

class QLabel;
class QPushButton;
class QListWidget;
class QScrollArea;
class QDialog;

class RadioWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit RadioWindow(QWidget *parent = nullptr);
    ~RadioWindow();

signals:
    void requestReturnToMain();

protected:
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

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
    void onScanTick();

private:
    void setupUI();
    void updateFrequencyView();
    void setupTopStatusIcons(QWidget *topBar);
    void rebuildStationStrip();
    void switchBand(bool fm);

    // ── V4L2 /dev/radio0 ─────────────────────────────────────────────────
    bool openDevice();
    void closeDevice();
    bool setFrequencyHz(quint32 freqHz);   // 单位：1/16 kHz（V4L2 标准）
    quint32 getFrequencyHz() const;
    bool setMute(bool mute);
    bool startAutoSeek(bool upward);       // VIDIOC_S_HW_FREQ_SEEK
    void stopScan();

    int     m_fd;           // /dev/radio0 文件描述符，-1 表示未打开

    // ── UI 控件 ──────────────────────────────────────────────────────────
    QLabel      *m_freqLabel;
    QLabel      *m_unitLabel;
    QLabel      *m_barLabel;
    QScrollArea *m_barScrollArea;
    QLabel      *m_scaleLabel;
    QPushButton *m_fmTabBtn;
    QPushButton *m_amTabBtn;
    QPushButton *m_searchBtn;
    QPushButton *m_playBtn;
    QPushButton *m_favoriteBtn;
    QPushButton *m_scanBtn;
    QListWidget *m_stationList;

    // ── 状态 ─────────────────────────────────────────────────────────────
    QStringList m_fmStations;
    QStringList m_amStations;
    bool    m_isFM;
    double  m_frequency;    // 用户可见频率（FM: MHz；AM: kHz）
    bool    m_tunerCapLow;  // 驱动是否使用 V4L2_TUNER_CAP_LOW 频率单位（62.5 Hz）
    int     m_tunerIndex;   // 当前使用的 V4L2 tuner 索引（0=FM/默认，部分驱动 1=AM）
    bool    m_favorite;
    bool    m_scanMode;     // 是否正在自动扫台
    bool    m_playing;

    QTimer *m_scanTimer;    // 扫台时轮询当前频率

    // ── 频率条拖拽状态 ────────────────────────────────────────────────────
    bool    m_barDragging;
    int     m_barDragStartX;
    int     m_barDragStartScroll;
};

#endif // RADIOWINDOW_H
