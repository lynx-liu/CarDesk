#include "imageviewingwindow.h"
#include "devicedetect.h"

#include <QCloseEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QSize>
#include <QStackedWidget>
#include <QTransform>
#include <QVBoxLayout>
#include <QApplication>
#include <QFileInfo>
#include <QIcon>
#include <QScreen>
#include <QTime>

ImageViewingWindow::ImageViewingWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_modeStack(nullptr)
    , m_titleLabel(nullptr)
    , m_viewTitleLabel(nullptr)
    , m_previewLabel(nullptr)
    , m_detailLabel(nullptr)
    , m_thumbnailList(nullptr)
    , m_prevButton(nullptr)
    , m_nextButton(nullptr)
    , m_rotateButton(nullptr)
    , m_currentIndex(0)
    , m_rotationAngle(0)
{
    setWindowTitle(QStringLiteral("图片浏览"));
    setObjectName("imageViewingWindow");
    setFixedSize(1280, 720);

    const DeviceDetect &device = DeviceDetect::instance();
    if (device.getDeviceType() == DeviceDetect::DEVICE_TYPE_CARUNIT) {
        setWindowState(Qt::WindowFullScreen);
    } else if (QApplication::primaryScreen()) {
        move(QApplication::primaryScreen()->geometry().center() - rect().center());
    }

    setupUI();
    updateImageView();
}

void ImageViewingWindow::closeEvent(QCloseEvent *event)
{
    emit requestReturnToMain();
    QMainWindow::closeEvent(event);
}

void ImageViewingWindow::onPrevImage()
{
    if (!m_thumbnailList || m_thumbnailList->count() == 0) {
        return;
    }
    m_currentIndex = (m_currentIndex + m_thumbnailList->count() - 1) % m_thumbnailList->count();
    m_thumbnailList->setCurrentRow(m_currentIndex);
    updateImageView();
}

void ImageViewingWindow::onNextImage()
{
    if (!m_thumbnailList || m_thumbnailList->count() == 0) {
        return;
    }
    m_currentIndex = (m_currentIndex + 1) % m_thumbnailList->count();
    m_thumbnailList->setCurrentRow(m_currentIndex);
    updateImageView();
}

void ImageViewingWindow::onThumbnailChanged(int row)
{
    if (row < 0) {
        return;
    }
    m_currentIndex = row;
    updateImageView();
}

void ImageViewingWindow::onOpenCurrentImage()
{
    if (!m_modeStack || !m_thumbnailList || m_thumbnailList->count() == 0) {
        return;
    }
    m_modeStack->setCurrentIndex(1);
    updateImageView();
}

void ImageViewingWindow::onBackToList()
{
    if (!m_modeStack) {
        return;
    }
    m_modeStack->setCurrentIndex(0);
}

void ImageViewingWindow::onRotateImage()
{
    m_rotationAngle = (m_rotationAngle + 90) % 360;
    updateImageView();
}

void ImageViewingWindow::setupUI()
{
    auto *central = new QWidget(this);
    setCentralWidget(central);
    central->setStyleSheet("QWidget{background: url(:/images/inside_background.png) no-repeat center center;color:#eaf2ff;}");

    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_modeStack = new QStackedWidget(central);
    m_modeStack->setStyleSheet("QStackedWidget{background:transparent;border:none;}");

    auto *listPage = new QWidget(m_modeStack);
    listPage->setStyleSheet("QWidget{background:transparent;border:none;}");

    QWidget *topBar = new QWidget(listPage);
    topBar->setFixedSize(1280, 82);
    topBar->setStyleSheet("background: url(:/images/topbar.png) no-repeat;");

    auto *homeBtn = new QPushButton(topBar);
    homeBtn->setGeometry(12, 12, 48, 48);
    homeBtn->setStyleSheet(
        "QPushButton { border:none; background-image:url(:/images/pict_home_up.png); background-repeat:no-repeat; }"
        "QPushButton:hover { background-image:url(:/images/pict_home_down.png); }"
    );
    homeBtn->setCursor(Qt::PointingHandCursor);
    connect(homeBtn, &QPushButton::clicked, this, [this]() {
        emit requestReturnToMain();
        hide();
    });

    m_titleLabel = new QLabel(QStringLiteral("图片浏览"), topBar);
    m_titleLabel->setGeometry(0, 0, 1280, 72);
    m_titleLabel->setStyleSheet("QLabel{color:#fff;font-size:36px;font-weight:700;background:transparent;}");
    m_titleLabel->setAlignment(Qt::AlignCenter);
    m_titleLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

    QWidget *right = new QWidget(topBar);
    right->setGeometry(1280 - 16 - 280, 12, 280, 48);
    right->setStyleSheet("background:transparent;");
    auto *rightLay = new QHBoxLayout(right);
    rightLay->setContentsMargins(0, 0, 0, 0);
    rightLay->setSpacing(16);

    auto *btIcon = new QLabel(right);
    btIcon->setFixedSize(48, 48);
    btIcon->setPixmap(QPixmap(":/images/pict_buetooth.png"));
    rightLay->addWidget(btIcon);

    auto *usbIcon = new QLabel(right);
    usbIcon->setFixedSize(48, 48);
    usbIcon->setPixmap(QPixmap(":/images/pict_usb.png"));
    rightLay->addWidget(usbIcon);

    auto *volIcon = new QLabel(right);
    volIcon->setFixedSize(48, 48);
    volIcon->setPixmap(QPixmap(":/images/pict_volume.png"));
    auto *volLabel = new QLabel(QStringLiteral("10"), right);
    volLabel->setStyleSheet("QLabel{color:#fff;font-size:36px;}");
    rightLay->addWidget(volIcon);
    rightLay->addWidget(volLabel);

    auto *timeLabel = new QLabel(QTime::currentTime().toString("hh:mm"), right);
    timeLabel->setStyleSheet("QLabel{color:#fff;font-size:36px;}");
    rightLay->addWidget(timeLabel);

    auto *backBtn = new QPushButton(listPage);
    backBtn->setGeometry(60, 103, 60, 60);
    backBtn->setStyleSheet(
        "QPushButton { border:none; background:url(:/images/butt_back_up.png) no-repeat; }"
        "QPushButton:hover { background:url(:/images/butt_back_down.png) no-repeat; }"
    );
    backBtn->setCursor(Qt::PointingHandCursor);

    connect(backBtn, &QPushButton::clicked, this, [this]() {
        if (m_modeStack && m_modeStack->currentIndex() == 1) {
            onBackToList();
            return;
        }
        emit requestReturnToMain();
        hide();
    });

    auto *contentLayout = new QVBoxLayout(listPage);
    contentLayout->setContentsMargins(168, 190, 168, 36);
    contentLayout->setSpacing(44);

    m_thumbnailList = new QListWidget(listPage);
    m_thumbnailList->setViewMode(QListView::IconMode);
    m_thumbnailList->setMovement(QListView::Static);
    m_thumbnailList->setResizeMode(QListView::Adjust);
    m_thumbnailList->setWrapping(true);
    m_thumbnailList->setSpacing(22);
    m_thumbnailList->setIconSize(QSize(120, 120));
    m_thumbnailList->setGridSize(QSize(160, 160));
    m_thumbnailList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_thumbnailList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_thumbnailList->setFixedHeight(356);
    m_thumbnailList->setStyleSheet(
        "QListWidget{border:none;background:transparent;outline:none;}"
        "QListWidget::item{border:none;color:#eaf3ff;font-size:20px;text-align:center;}"
        "QListWidget::item:selected{color:#00faff;}"
        "QListWidget::item:hover{color:#dff9ff;}"
    );

    const QStringList imageItems = {
        QStringLiteral("图片"),
        QStringLiteral("There"),
        QStringLiteral("新建文件夹"),
        QStringLiteral("浮夸"),
        QStringLiteral("浮夸"),
        QStringLiteral("浮夸"),
        QStringLiteral("浮夸"),
        QStringLiteral("浮夸"),
        QStringLiteral("浮夸"),
        QStringLiteral("浮夸")
    };
    for (int i = 0; i < imageItems.size(); ++i) {
        const QString &name = imageItems.at(i);
        auto *it = new QListWidgetItem(name, m_thumbnailList);
        it->setSizeHint(QSize(160, 160));
        it->setTextAlignment(Qt::AlignHCenter | Qt::AlignTop);
        if (i < 3) {
            it->setData(Qt::UserRole, true);
        }
        m_thumbnailList->addItem(it);
    }

    const QIcon folderUp(QStringLiteral(":/images/butt_driving_image_playback_folder_up.png"));
    const QIcon folderDown(QStringLiteral(":/images/butt_driving_image_playback_folder_down.png"));
    const QIcon fileUp(QStringLiteral(":/images/butt_driving_image_playback_filelist_up.png"));
    const QIcon fileDown(QStringLiteral(":/images/butt_driving_image_playback_filelist_down.png"));

    auto refreshListIcons = [this, folderUp, folderDown, fileUp, fileDown]() {
        if (!m_thumbnailList) {
            return;
        }
        const int currentRow = m_thumbnailList->currentRow();
        for (int row = 0; row < m_thumbnailList->count(); ++row) {
            QListWidgetItem *item = m_thumbnailList->item(row);
            if (!item) {
                continue;
            }
            const bool isFolder = item->data(Qt::UserRole).toBool();
            const bool isCurrent = (row == currentRow);
            if (isFolder) {
                item->setIcon(isCurrent ? folderDown : folderUp);
            } else {
                item->setIcon(isCurrent ? fileDown : fileUp);
            }
        }
    };

    m_detailLabel = new QLabel(listPage);
    m_detailLabel->setFixedHeight(50);
    m_detailLabel->setStyleSheet("QLabel{background:rgba(255,255,255,0.1);border:1px solid #0068ff;border-radius:5px;padding-left:24px;font-size:24px;color:#eaf4ff;}");

    contentLayout->addWidget(m_thumbnailList, 0);
    contentLayout->addWidget(m_detailLabel);
    contentLayout->addStretch(1);

    auto *viewPage = new QWidget(m_modeStack);
    viewPage->setStyleSheet("QWidget{background:#000;border:none;}");

    m_previewLabel = new QLabel(viewPage);
    m_previewLabel->setGeometry(0, 0, 1280, 720);
    m_previewLabel->setAlignment(Qt::AlignCenter);
    m_previewLabel->setStyleSheet("QLabel{background:#000;}");

    auto *overlayTop = new QWidget(m_previewLabel);
    overlayTop->setGeometry(0, 0, 1280, 72);
    overlayTop->setStyleSheet("background:rgba(0,0,0,0.5);");

    auto *viewBack = new QPushButton(overlayTop);
    viewBack->setGeometry(12, 12, 48, 48);
    viewBack->setStyleSheet(
        "QPushButton{border:none;background-image:url(:/images/butt_video_back_up.png);background-repeat:no-repeat;background-position:center;}"
        "QPushButton:hover{background-image:url(:/images/butt_video_back_down.png);}"
    );
    connect(viewBack, &QPushButton::clicked, this, &ImageViewingWindow::onBackToList);

    m_viewTitleLabel = new QLabel(QStringLiteral("图片"), overlayTop);
    m_viewTitleLabel->setGeometry(100, 0, 1080, 72);
    m_viewTitleLabel->setStyleSheet("QLabel{color:#fff;font-size:36px;font-weight:700;background:transparent;}");
    m_viewTitleLabel->setAlignment(Qt::AlignCenter);

    auto *bottomBar = new QWidget(m_previewLabel);
    bottomBar->setGeometry(0, 588, 1280, 132);
    bottomBar->setStyleSheet("background:rgba(0,0,0,0.5);");
    auto *btnLayout = new QHBoxLayout(bottomBar);
    btnLayout->setContentsMargins(0, 24, 0, 24);
    btnLayout->setSpacing(0);

    auto *btnWrap = new QWidget(bottomBar);
    btnWrap->setFixedWidth(420);
    auto *btnWrapLay = new QHBoxLayout(btnWrap);
    btnWrapLay->setContentsMargins(0, 0, 0, 0);
    btnWrapLay->setSpacing(0);

    m_prevButton = new QPushButton(btnWrap);
    m_prevButton->setFixedSize(60, 60);
    m_prevButton->setStyleSheet(
        "QPushButton{border:none;background-image:url(:/images/butt_music_prev_up.png);background-repeat:no-repeat;background-position:center;}"
        "QPushButton:hover{background-image:url(:/images/butt_music_prev_down.png);}"
    );
    connect(m_prevButton, &QPushButton::clicked, this, &ImageViewingWindow::onPrevImage);

    auto *hiddenCenter = new QPushButton(btnWrap);
    hiddenCenter->setFixedSize(84, 84);
    hiddenCenter->setVisible(false);

    m_nextButton = new QPushButton(btnWrap);
    m_nextButton->setFixedSize(60, 60);
    m_nextButton->setStyleSheet(
        "QPushButton{border:none;background-image:url(:/images/butt_music_next_up.png);background-repeat:no-repeat;background-position:center;}"
        "QPushButton:hover{background-image:url(:/images/butt_music_next_down.png);}"
    );
    connect(m_nextButton, &QPushButton::clicked, this, &ImageViewingWindow::onNextImage);

    m_rotateButton = new QPushButton(btnWrap);
    m_rotateButton->setFixedSize(60, 60);
    m_rotateButton->setStyleSheet(
        "QPushButton{border:none;background-image:url(:/images/butt_image_rotate_up.png);background-repeat:no-repeat;background-position:center;}"
        "QPushButton:hover{background-image:url(:/images/butt_image_rotate_down.png);}"
    );
    connect(m_rotateButton, &QPushButton::clicked, this, &ImageViewingWindow::onRotateImage);

    btnWrapLay->addWidget(m_prevButton);
    btnWrapLay->addStretch();
    btnWrapLay->addWidget(hiddenCenter);
    btnWrapLay->addStretch();
    btnWrapLay->addWidget(m_nextButton);
    btnWrapLay->addStretch();
    btnWrapLay->addWidget(m_rotateButton);

    btnLayout->addStretch();
    btnLayout->addWidget(btnWrap);
    btnLayout->addStretch();

    m_modeStack->addWidget(listPage);
    m_modeStack->addWidget(viewPage);

    root->addWidget(m_modeStack, 1);

    connect(m_thumbnailList, &QListWidget::currentRowChanged, this, &ImageViewingWindow::onThumbnailChanged);
    connect(m_thumbnailList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *) {
        onOpenCurrentImage();
    });
    connect(m_thumbnailList, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        if (!item) {
            return;
        }
        if (!item->data(Qt::UserRole).toBool()) {
            onOpenCurrentImage();
        }
    });
    m_thumbnailList->setCurrentRow(0);
    refreshListIcons();
    connect(m_thumbnailList, &QListWidget::currentRowChanged, this, [refreshListIcons](int) {
        refreshListIcons();
    });
    m_modeStack->setCurrentIndex(0);
}

void ImageViewingWindow::updateImageView()
{
    if (!m_thumbnailList || !m_titleLabel || !m_detailLabel || m_thumbnailList->count() == 0) {
        return;
    }

    const QString fileName = m_thumbnailList->item(m_currentIndex)->text();
    m_titleLabel->setText(QStringLiteral("图片浏览"));
    m_detailLabel->setText(QStringLiteral("USB > 图片 > 风景"));

    if (m_viewTitleLabel) {
        QFileInfo info(fileName);
        m_viewTitleLabel->setText(info.completeBaseName().isEmpty() ? fileName : info.completeBaseName());
    }

    if (m_previewLabel) {
        QPixmap source(":/images/image_view.png");
        if (source.isNull()) {
            source = QPixmap(":/images/background.png");
        }

        if (!source.isNull()) {
            QTransform transform;
            transform.rotate(m_rotationAngle);
            const QPixmap rotated = source.transformed(transform, Qt::SmoothTransformation);
            m_previewLabel->setPixmap(rotated.scaled(m_previewLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
        }
    }
}
