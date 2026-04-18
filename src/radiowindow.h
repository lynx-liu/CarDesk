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
    bool startAutoSeek(bool upward);       // 用户空间逐频点 seek
    void stopScan();
    void updateTunerStatus();              // 从硬件读信号/立体声状态

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
    QPushButton *m_favoriteBtn;
    QPushButton *m_scanBtn;
    QListWidget *m_stationList;
    QLabel      *m_stereoLabel;     // STEREO 指示

    // ── 状态 ─────────────────────────────────────────────────────────────
    QStringList m_fmStations;
    QStringList m_amStations;
    QStringList m_fmFavorites;  // 用户收藏的 FM 频率（如 "95.9"）
    QStringList m_amFavorites;  // 用户收藏的 AM 频率（如 "990"）
    bool    m_isFM;
    double  m_frequency;    // 用户可见频率（FM: MHz；AM: kHz）
    bool    m_tunerCapLow;  // 驱动是否使用 V4L2_TUNER_CAP_LOW 频率单位（62.5 Hz）
    int     m_tunerIndex;   // 当前使用的 V4L2 tuner 索引（0=FM/默认，部分驱动 1=AM）
    bool    m_favorite;
    bool    m_scanMode;     // 是否正在自动扫台（连续扫台模式）
    
    // ── 搜台（用户空间逐频点检测）────────────────────────────────────────
    bool    m_seekUpward;       // 搜台方向
    double  m_seekStartFreq;    // 本次搜台起始频率（用于绕圈检测）
    int     m_seekStepCount;    // 已扫描步数

    QTimer *m_scanTimer;    // 搜台步进计时器（每 200ms 一步）

    // ── 频率条拖拽状态 ────────────────────────────────────────────────────
    bool    m_barDragging;
    int     m_barDragStartX;
    int     m_barDragStartScroll;
};

#endif // RADIOWINDOW_H
