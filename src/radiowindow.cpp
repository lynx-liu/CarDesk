#include "radiowindow.h"
#include "devicedetect.h"

#include <QApplication>
#include <QCloseEvent>
#include <QDateTime>
#include <QDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QScreen>
#include <QLineEdit>
#include <QVBoxLayout>

RadioWindow::RadioWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_freqLabel(nullptr)
    , m_unitLabel(nullptr)
    , m_barLabel(nullptr)
    , m_scaleLabel(nullptr)
    , m_fmTabBtn(nullptr)
    , m_amTabBtn(nullptr)
    , m_searchBtn(nullptr)
    , m_playBtn(nullptr)
    , m_favoriteBtn(nullptr)
    , m_scanBtn(nullptr)
    , m_stationList(nullptr)
    , m_fmStations({"88.7", "90.6", "91.2", "92.5", "95.9", "96.3", "97.7", "99.8", "101.1"})
    , m_amStations({"554", "639", "756", "855", "937", "955", "981", "1008", "1143"})
    , m_isFM(true)
    , m_frequency(95.9)
    , m_favorite(true)
    , m_scanMode(false)
    , m_playing(false) {
    setWindowTitle("收音机");
    setFixedSize(1280, 720);

    const DeviceDetect &device = DeviceDetect::instance();
    if (device.getDeviceType() == DeviceDetect::DEVICE_TYPE_CARUNIT) {
        setWindowState(Qt::WindowFullScreen);
    } else if (QApplication::primaryScreen()) {
        move(QApplication::primaryScreen()->geometry().center() - rect().center());
    }

    setupUI();
    updateFrequencyView();
}

void RadioWindow::closeEvent(QCloseEvent *event) {
    emit requestReturnToMain();
    QMainWindow::closeEvent(event);
}

void RadioWindow::setupUI() {
    QWidget *central = new QWidget(this);
    central->setStyleSheet("background-image:url(:/images/inside_background.png);background-repeat:no-repeat;");
    setCentralWidget(central);

    QVBoxLayout *main = new QVBoxLayout(central);
    main->setContentsMargins(0, 0, 0, 0);
    main->setSpacing(0);

    QWidget *topBar = new QWidget(this);
    topBar->setFixedHeight(82);
    topBar->setStyleSheet("background-image:url(:/images/topbar.png);");
    QGridLayout *top = new QGridLayout(topBar);
    top->setContentsMargins(16, 0, 16, 0);
    top->setColumnStretch(0, 1);
    top->setColumnStretch(1, 0);
    top->setColumnStretch(2, 1);

    QPushButton *homeBtn = new QPushButton(this);
    homeBtn->setFixedSize(48, 48);
    homeBtn->setStyleSheet("QPushButton{border:none;background-image:url(:/images/pict_home_up.png);} QPushButton:hover{background-image:url(:/images/pict_home_down.png);}");
    connect(homeBtn, &QPushButton::clicked, this, [this](){ emit requestReturnToMain(); close(); });
    top->addWidget(homeBtn, 0, 0, Qt::AlignLeft | Qt::AlignVCenter);

    QLabel *title = new QLabel("收音机", this);
    title->setStyleSheet("color:#fff;font-size:36px;font-weight:bold;background:transparent;");
    top->addWidget(title, 0, 1, Qt::AlignCenter);

    setupTopStatusIcons(topBar);

    main->addWidget(topBar);

    QWidget *tabWrap = new QWidget(this);
    tabWrap->setFixedHeight(66);
    QHBoxLayout *tabLayout = new QHBoxLayout(tabWrap);
    tabLayout->setContentsMargins(480, 0, 0, 0);
    tabLayout->setSpacing(0);

    m_fmTabBtn = new QPushButton("FM", tabWrap);
    m_amTabBtn = new QPushButton("AM", tabWrap);
    m_fmTabBtn->setCheckable(true);
    m_amTabBtn->setCheckable(true);
    m_fmTabBtn->setFixedSize(160, 66);
    m_amTabBtn->setFixedSize(160, 66);
    connect(m_fmTabBtn, &QPushButton::clicked, this, &RadioWindow::onSwitchFM);
    connect(m_amTabBtn, &QPushButton::clicked, this, &RadioWindow::onSwitchAM);
    tabLayout->addWidget(m_fmTabBtn);
    tabLayout->addWidget(m_amTabBtn);
    tabLayout->addStretch();

    main->addWidget(tabWrap);

    QWidget *content = new QWidget(this);
    QVBoxLayout *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(70, 20, 70, 10);
    contentLayout->setSpacing(16);

    QWidget *freqRow = new QWidget(this);
    QHBoxLayout *freqLayout = new QHBoxLayout(freqRow);
    freqLayout->setContentsMargins(0, 0, 0, 0);

    QLabel *stereo = new QLabel("STEREO", this);
    stereo->setStyleSheet("color:#00FAFF;font-size:36px;");
    freqLayout->addWidget(stereo);

    m_freqLabel = new QLabel(this);
    m_freqLabel->setStyleSheet("color:#fff;font-size:120px;");
    freqLayout->addWidget(m_freqLabel);

    m_unitLabel = new QLabel("MHz", this);
    m_unitLabel->setStyleSheet("color:#fff;font-size:48px;");
    freqLayout->addWidget(m_unitLabel);

    freqLayout->addStretch();

    contentLayout->addWidget(freqRow);

    QWidget *ctrlRow = new QWidget(this);
    ctrlRow->setFixedWidth(1056);
    QHBoxLayout *ctrlLayout = new QHBoxLayout(ctrlRow);
    ctrlLayout->setContentsMargins(0, 0, 0, 0);
    ctrlLayout->setSpacing(0);

    QPushButton *prev = new QPushButton(this);
    prev->setFixedSize(120, 120);
    prev->setStyleSheet("QPushButton{border:none;background-image:url(:/images/butt_radio_searchpre_up.png);} QPushButton:hover{background-image:url(:/images/butt_radio_searchpre_down.png);}");
    connect(prev, &QPushButton::clicked, this, &RadioWindow::onPrev);
    ctrlLayout->addWidget(prev);

    QWidget *barWrap = new QWidget(this);
    barWrap->setFixedSize(720, 106);
    barWrap->setStyleSheet("background:transparent;");

    m_barLabel = new QLabel(barWrap);
    m_barLabel->setGeometry(0, 0, 2160, 106);
    m_barLabel->setStyleSheet("background:transparent;");

    QLabel *leftMask = new QLabel(barWrap);
    leftMask->setGeometry(46, 0, 64, 106);
    leftMask->setPixmap(QPixmap(":/images/pict_radio_barmask_left.png"));

    QLabel *rightMask = new QLabel(barWrap);
    rightMask->setGeometry(610, 0, 64, 106);
    rightMask->setPixmap(QPixmap(":/images/pict_radio_barmask_right.png"));

    QLabel *mark = new QLabel(barWrap);
    mark->setGeometry(404, 10, 8, 85);
    mark->setPixmap(QPixmap(":/images/pict_radio_mark.png"));

    ctrlLayout->addWidget(barWrap);

    QPushButton *next = new QPushButton(this);
    next->setFixedSize(120, 120);
    next->setStyleSheet("QPushButton{border:none;background-image:url(:/images/butt_radio_searchnext_up.png);} QPushButton:hover{background-image:url(:/images/butt_radio_searchnext_down.png);}");
    connect(next, &QPushButton::clicked, this, &RadioWindow::onNext);
    ctrlLayout->addWidget(next);

    contentLayout->addWidget(ctrlRow, 0, Qt::AlignCenter);

    QWidget *btnRow = new QWidget(this);
    btnRow->setFixedWidth(804);
    QHBoxLayout *btnLayout = new QHBoxLayout(btnRow);
    btnLayout->setContentsMargins(0, 0, 0, 0);
    btnLayout->setSpacing(0);

    QPushButton *listBtn = new QPushButton(this);
    listBtn->setFixedSize(60, 60);
    listBtn->setStyleSheet("QPushButton{border:none;background-image:url(:/images/butt_radio_list_up.png);} QPushButton:hover{background-image:url(:/images/butt_radio_list_down.png);}");
    connect(listBtn, &QPushButton::clicked, this, &RadioWindow::onOpenListDialog);
    btnLayout->addWidget(listBtn);
    btnLayout->addStretch();

    m_searchBtn = new QPushButton(this);
    m_searchBtn->setFixedSize(60, 60);
    m_searchBtn->setStyleSheet("QPushButton{border:none;background-image:url(:/images/butt_radio_search_up.png);} QPushButton:hover{background-image:url(:/images/butt_radio_search_down.png);}");
    connect(m_searchBtn, &QPushButton::clicked, this, &RadioWindow::onSearch);
    btnLayout->addWidget(m_searchBtn);
    btnLayout->addStretch();

    m_playBtn = new QPushButton(this);
    m_playBtn->setFixedSize(84, 84);
    m_playBtn->setCursor(Qt::PointingHandCursor);
    m_playBtn->setStyleSheet("QPushButton{border:none;background-image:url(:/images/butt_music_play_up.png);} QPushButton:hover{background-image:url(:/images/butt_music_stop_up.png);}");
    connect(m_playBtn, &QPushButton::clicked, this, &RadioWindow::onTogglePlay);
    btnLayout->addWidget(m_playBtn);
    btnLayout->addStretch();

    m_favoriteBtn = new QPushButton(this);
    m_favoriteBtn->setFixedSize(60, 60);
    connect(m_favoriteBtn, &QPushButton::clicked, this, &RadioWindow::onToggleFavorite);
    btnLayout->addWidget(m_favoriteBtn);
    btnLayout->addStretch();

    m_scanBtn = new QPushButton(this);
    m_scanBtn->setFixedSize(60, 60);
    connect(m_scanBtn, &QPushButton::clicked, this, &RadioWindow::onToggleScan);
    btnLayout->addWidget(m_scanBtn);

    contentLayout->addWidget(btnRow, 0, Qt::AlignHCenter);

    m_stationList = new QListWidget(this);
    m_stationList->setFlow(QListView::LeftToRight);
    m_stationList->setWrapping(false);
    m_stationList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_stationList->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_stationList->setFixedSize(1118, 120);
    m_stationList->setIconSize(QSize(1, 1));
    m_stationList->setGridSize(QSize(150, 118));
    m_stationList->setSpacing(0);
    m_stationList->setStyleSheet(
        "QListWidget{background:transparent;border:none;outline:none;}"
        "QListWidget::item{width:150px;height:118px;background-image:url(:/images/radio_play_list_up.png);background-repeat:no-repeat;background-position:center;"
        "font-size:36px;color:#fff;text-align:center;padding-top:22px;}"
        "QListWidget::item:selected{background-image:url(:/images/radio_play_list_down.png);color:#00faff;}"
        "QListWidget::item:hover{background-image:url(:/images/radio_play_list_down.png);color:#00faff;}"
    );
    rebuildStationStrip();

    connect(m_stationList, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        m_frequency = item->text().toDouble();
        updateFrequencyView();
    });

    contentLayout->addWidget(m_stationList, 0, Qt::AlignHCenter);
    contentLayout->addStretch();

    main->addWidget(content, 1);
    switchBand(true);
}

void RadioWindow::updateFrequencyView() {
    if (m_fmTabBtn && m_amTabBtn) {
        m_fmTabBtn->setChecked(m_isFM);
        m_amTabBtn->setChecked(!m_isFM);
        m_fmTabBtn->setStyleSheet(m_isFM
            ? "QPushButton{border:none;background:url(:/images/butt_tab_left_on.png);color:#fff;font-size:28px;}"
            : "QPushButton{border:none;background:url(:/images/butt_tab_left_down.png);color:#fff;font-size:28px;}");
        m_amTabBtn->setStyleSheet(!m_isFM
            ? "QPushButton{border:none;background:url(:/images/butt_tab_right_on.png);color:#fff;font-size:28px;}"
            : "QPushButton{border:none;background:url(:/images/butt_tab_right_down.png);color:#fff;font-size:28px;}");
    }

    if (m_freqLabel) {
        m_freqLabel->setText(m_isFM ? QString::number(m_frequency, 'f', 1)
                                    : QString::number(m_frequency, 'f', 0));
    }
    if (m_unitLabel) {
        m_unitLabel->setText("MHz");
    }
    if (m_barLabel) {
        const QString barPath = m_isFM ? QStringLiteral(":/images/pict_radio_fmbar.png")
                                       : QStringLiteral(":/images/pict_radio_ambar.png");
        const QPixmap barPixmap(barPath);
        if (!barPixmap.isNull()) {
            const int viewportWidth = 720;
            const int markerX = 408;
            const int barWidth = barPixmap.width();
            const double minFreq = m_isFM ? 87.0 : 522.0;
            const double maxFreq = m_isFM ? 108.0 : 1710.0;
            const double clamped = qBound(minFreq, m_frequency, maxFreq);
            const double ratio = (clamped - minFreq) / (maxFreq - minFreq);
            const int targetX = markerX - static_cast<int>(ratio * barWidth);
            const int minX = -(barWidth - viewportWidth);
            const int x = qBound(minX, targetX, 0);

            m_barLabel->setPixmap(barPixmap);
            m_barLabel->setFixedSize(barWidth, 106);
            m_barLabel->move(x, 0);
        }
    }

    if (m_stationList) {
        const QString needle = m_isFM ? QString::number(m_frequency, 'f', 1) : QString::number(m_frequency, 'f', 0);
        QList<QListWidgetItem *> items = m_stationList->findItems(needle, Qt::MatchExactly);
        if (!items.isEmpty()) {
            m_stationList->setCurrentItem(items.first());
        }
    }
    if (m_favoriteBtn) {
        m_favoriteBtn->setStyleSheet(m_favorite
            ? "QPushButton{border:none;background-image:url(:/images/butt_music_collection_up.png);} QPushButton:hover{background-image:url(:/images/butt_music_collection_down.png);}"
            : "QPushButton{border:none;background-image:url(:/images/butt_music_scan_up.png);} QPushButton:hover{background-image:url(:/images/butt_music_scan_down.png);}"
        );
    }
    if (m_scanBtn) {
        m_scanBtn->setStyleSheet(m_scanMode
            ? "QPushButton{border:none;background-image:url(:/images/butt_music_scan_down.png);}"
            : "QPushButton{border:none;background-image:url(:/images/butt_music_scan_up.png);} QPushButton:hover{background-image:url(:/images/butt_music_scan_down.png);}"
        );
    }
}

void RadioWindow::onSwitchFM() {
    switchBand(true);
}

void RadioWindow::onSwitchAM() {
    switchBand(false);
}

void RadioWindow::onPrev() {
    const QStringList stations = m_isFM ? m_fmStations : m_amStations;
    const QString now = m_isFM ? QString::number(m_frequency, 'f', 1) : QString::number(m_frequency, 'f', 0);
    int idx = stations.indexOf(now);
    if (idx < 0) {
        idx = 0;
    }
    idx = (idx + stations.size() - 1) % stations.size();
    m_frequency = stations[idx].toDouble();
    updateFrequencyView();
}

void RadioWindow::onNext() {
    const QStringList stations = m_isFM ? m_fmStations : m_amStations;
    const QString now = m_isFM ? QString::number(m_frequency, 'f', 1) : QString::number(m_frequency, 'f', 0);
    int idx = stations.indexOf(now);
    if (idx < 0) {
        idx = 0;
    }
    idx = (idx + 1) % stations.size();
    m_frequency = stations[idx].toDouble();
    updateFrequencyView();
}

void RadioWindow::onToggleFavorite() {
    m_favorite = !m_favorite;
    updateFrequencyView();
}

void RadioWindow::onToggleScan() {
    m_scanMode = !m_scanMode;
    updateFrequencyView();
}

void RadioWindow::onTogglePlay() {
    m_playing = !m_playing;
    if (m_playBtn) {
        m_playBtn->setStyleSheet(m_playing
            ? "QPushButton{border:none;background-image:url(:/images/butt_music_stop_up.png);} QPushButton:hover{background-image:url(:/images/butt_music_play_up.png);}"
            : "QPushButton{border:none;background-image:url(:/images/butt_music_play_up.png);} QPushButton:hover{background-image:url(:/images/butt_music_stop_up.png);}"
        );
    }
}

void RadioWindow::onSearch() {
    QDialog dialog(this);
    dialog.setWindowTitle("搜索频率");
    dialog.setFixedSize(860, 560);
    dialog.setStyleSheet("QDialog{background:#0d1f3f;border:1px solid #0068ff;color:#fff;}");

    QVBoxLayout *root = new QVBoxLayout(&dialog);
    root->setContentsMargins(22, 18, 22, 18);
    root->setSpacing(10);

    QLineEdit *input = new QLineEdit(m_isFM ? QString::number(m_frequency, 'f', 1) : QString::number(m_frequency, 'f', 0), &dialog);
    input->setStyleSheet("QLineEdit{height:72px;border:1px solid #0068ff;color:#fff;font-size:48px;padding-left:88px;background:url(:/images/butt_radiolist_search_up.png) no-repeat 24px center;}");
    root->addWidget(input);

    QWidget *keypadWrap = new QWidget(&dialog);
    QHBoxLayout *keypadRoot = new QHBoxLayout(keypadWrap);
    keypadRoot->setContentsMargins(0, 0, 0, 0);
    keypadRoot->setSpacing(8);

    QWidget *gridWrap = new QWidget(&dialog);
    QGridLayout *grid = new QGridLayout(gridWrap);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(8);
    const QStringList keys = {"1", "2", "3", "4", "5", "6", "7", "8", "9", ".", "0", "<-"};
    for (int i = 0; i < keys.size(); ++i) {
        QPushButton *btn = new QPushButton(keys[i], &dialog);
        btn->setFixedSize(198, 94);
        btn->setStyleSheet("QPushButton{border:1px solid #0068ff;color:#fff;font-size:48px;font-weight:700;background:transparent;}QPushButton:hover{border-color:#00faff;color:#00faff;}");
        grid->addWidget(btn, i / 3, i % 3);
        connect(btn, &QPushButton::clicked, &dialog, [input, btn]() {
            if (btn->text() == "<-") {
                input->setText(input->text().left(qMax(0, input->text().size() - 1)));
            } else {
                input->setText(input->text() + btn->text());
            }
        });
    }
    keypadRoot->addWidget(gridWrap);

    QPushButton *confirm = new QPushButton("确认", &dialog);
    confirm->setFixedSize(198, 400);
    confirm->setStyleSheet("QPushButton{border:none;background:#0068ff;color:#fff;font-size:48px;font-weight:700;}QPushButton:hover{background:#00faff;}");
    connect(confirm, &QPushButton::clicked, &dialog, &QDialog::accept);
    keypadRoot->addWidget(confirm);
    root->addWidget(keypadWrap);

    if (dialog.exec() == QDialog::Accepted) {
        bool ok = false;
        const double v = input->text().toDouble(&ok);
        if (!ok) {
            return;
        }
        if (m_isFM) {
            m_frequency = qBound(87.5, v, 108.0);
        } else {
            m_frequency = qBound(531.0, v, 1602.0);
        }
        updateFrequencyView();
    }
}

void RadioWindow::onOpenListDialog() {
    QDialog dialog(this);
    dialog.setWindowTitle("电台列表");
    dialog.setFixedSize(1120, 620);
    dialog.setStyleSheet("QDialog{background:#0d1f3f;border:1px solid #0068ff;color:#fff;}");

    QVBoxLayout *root = new QVBoxLayout(&dialog);
    root->setContentsMargins(20, 20, 20, 20);

    QListWidget *list = new QListWidget(&dialog);
    list->setViewMode(QListView::IconMode);
    list->setResizeMode(QListView::Adjust);
    list->setMovement(QListView::Static);
    list->setWrapping(true);
    list->setGridSize(QSize(212, 212));
    list->setIconSize(QSize(1, 1));
    list->setStyleSheet(
        "QListWidget{border:none;background:transparent;outline:none;}"
        "QListWidget::item{width:212px;height:212px;background-image:url(:/images/radio_list_up.png);background-repeat:no-repeat;background-position:center;"
        "font-size:48px;color:#fff;text-align:center;padding-top:52px;}"
        "QListWidget::item:selected{background-image:url(:/images/radio_list_down.png);color:#00faff;}"
        "QListWidget::item:hover{background-image:url(:/images/radio_list_down.png);color:#00faff;}"
    );
    const QStringList stations = m_isFM ? m_fmStations : m_amStations;
    for (const QString &s : stations) {
        list->addItem(s);
    }

    connect(list, &QListWidget::itemDoubleClicked, &dialog, [&dialog](QListWidgetItem *) {
        dialog.accept();
    });

    root->addWidget(list);
    if (dialog.exec() == QDialog::Accepted && list->currentItem()) {
        m_frequency = list->currentItem()->text().toDouble();
        updateFrequencyView();
    }
}

void RadioWindow::setupTopStatusIcons(QWidget *topBar) {
    QWidget *right = new QWidget(topBar);
    right->setGeometry(1280 - 16 - 280, 12, 280, 48);
    right->setStyleSheet("background:transparent;");
    QHBoxLayout *rightLay = new QHBoxLayout(right);
    rightLay->setContentsMargins(0, 0, 0, 0);
    rightLay->setSpacing(16);

    QLabel *btIcon = new QLabel(right);
    btIcon->setFixedSize(48, 48);
    btIcon->setPixmap(QPixmap(":/images/pict_bluetooth.png"));
    rightLay->addWidget(btIcon);

    QLabel *usbIcon = new QLabel(right);
    usbIcon->setFixedSize(48, 48);
    usbIcon->setPixmap(QPixmap(":/images/pict_usb.png"));
    rightLay->addWidget(usbIcon);

    QLabel *volIcon = new QLabel(right);
    volIcon->setFixedSize(48, 48);
    volIcon->setPixmap(QPixmap(":/images/pict_volume.png"));
    QLabel *volLabel = new QLabel("10", right);
    volLabel->setStyleSheet("QLabel{color:#fff;font-size:36px;}");
    rightLay->addWidget(volIcon);
    rightLay->addWidget(volLabel);

    QLabel *timeLabel = new QLabel(QDateTime::currentDateTime().toString("hh:mm"), right);
    timeLabel->setStyleSheet("QLabel{color:#fff;font-size:36px;}");
    rightLay->addWidget(timeLabel);
}

void RadioWindow::rebuildStationStrip() {
    if (!m_stationList) {
        return;
    }
    m_stationList->clear();
    const QStringList stations = m_isFM ? m_fmStations : m_amStations;
    for (const QString &s : stations) {
        QListWidgetItem *it = new QListWidgetItem(s, m_stationList);
        it->setSizeHint(QSize(150, 118));
        m_stationList->addItem(it);
    }
}

void RadioWindow::switchBand(bool fm) {
    m_isFM = fm;
    if (m_isFM) {
        m_frequency = 95.9;
    } else {
        m_frequency = 937.0;
    }
    rebuildStationStrip();
    updateFrequencyView();
}
