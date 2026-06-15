#include "imageviewingwindow.h"
#include "pagebgwidget.h"
#include "devicedetect.h"
#include "topbarwidget.h"

#include <QCloseEvent>
#include <QKeyEvent>
#include <QProcess>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QScrollBar>
#include <QSize>
#include <QStackedWidget>
#include <QTransform>
#include <QVBoxLayout>
#include <QApplication>
#include <QIcon>
#include <QScreen>
#include <QShowEvent>
#include <QTime>
#include <QStorageInfo>
#include <QGesture>
#include <QGestureEvent>
#include "appsignals.h"
#include "imageloader.h"

static const QString kUsbMountDir    = QStringLiteral("/mnt/usb");
static const QString kUsbMountPrefix = QStringLiteral("/mnt/usb/");

static QString findFirstUsbImageDirectory();

ImageViewingWindow::ImageViewingWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_modeStack(nullptr)
    , m_viewPage(nullptr)
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
    , m_zoomFactor(1.0)
    , m_isPinching(false)
    , m_thumbLoader(nullptr)
    , m_loaderThread(nullptr)
    , m_cachedRotation(-1)
    , m_currentPath(QStringLiteral("/mnt"))
    , m_initialPath(QStringLiteral("/mnt"))
{
    setWindowTitle(QStringLiteral("图片浏览"));
    setObjectName("imageViewingWindow");
    setFixedSize(1280, 720);

    m_imageExtensions << QStringLiteral("*.jpg") << QStringLiteral("*.jpeg")
                      << QStringLiteral("*.png") << QStringLiteral("*.bmp")
                      << QStringLiteral("*.gif") << QStringLiteral("*.webp");

    const DeviceDetect &device = DeviceDetect::instance();
    if (device.getDeviceType() == DeviceDetect::DEVICE_TYPE_CARUNIT) {
        setWindowState(Qt::WindowFullScreen);
    } else if (QApplication::primaryScreen()) {
        move(QApplication::primaryScreen()->geometry().center() - rect().center());
    }

    setupUI();

    // Async thumbnail loader
    m_loaderThread = new QThread(this);
    m_thumbLoader  = new ThumbnailLoader();
    m_thumbLoader->moveToThread(m_loaderThread);
    connect(m_thumbLoader, &ThumbnailLoader::thumbnailReady,
            this, &ImageViewingWindow::onThumbnailReady);
    connect(m_loaderThread, &QThread::finished,
            m_thumbLoader, &QObject::deleteLater);
    m_loaderThread->start();

    const QString usbRoot = findFirstUsbImageDirectory();
    if (!usbRoot.isEmpty()) {
        m_initialPath = usbRoot;
        m_currentPath = usbRoot;
    }

    loadDirectory(m_initialPath);

    // USB 插拔时更新显示
    connect(AppSignals::instance(), &AppSignals::usbStateChanged, this, [this](bool connected) {
        if (!connected) {
            m_currentPath.clear();
            m_imageFiles.clear();
            if (m_thumbnailList) m_thumbnailList->clear();
            if (m_detailLabel) m_detailLabel->setText(QStringLiteral("请插入U盘"));
        } else if (m_modeStack && m_modeStack->currentIndex() == 0) {
            // 在列表页时插入 U 盘，自动刷新
            const QString usbPath = findFirstUsbImageDirectory();
            if (!usbPath.isEmpty()) {
                m_initialPath = usbPath;
                loadDirectory(usbPath);
            }
        }
    });
}

void ImageViewingWindow::closeEvent(QCloseEvent *event)
{
    if (m_thumbLoader) m_thumbLoader->cancel();
    if (m_loaderThread) { m_loaderThread->quit(); m_loaderThread->wait(500); }
    emit requestReturnToMain();
    QMainWindow::closeEvent(event);
}

void ImageViewingWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    if (m_currentPath.isEmpty() || m_currentPath == QStringLiteral("/mnt")
            || m_currentPath == kUsbMountDir) {
        const QString usbRoot = findFirstUsbImageDirectory();
        if (!usbRoot.isEmpty()) {
            m_initialPath = usbRoot;
            loadDirectory(usbRoot);
        }
    }
}

void ImageViewingWindow::onPrevImage()
{
    if (m_imageFiles.isEmpty()) return;
    m_zoomFactor = 1.0;
    m_currentIndex = (m_currentIndex + m_imageFiles.count() - 1) % m_imageFiles.count();
    updateImageView();
}

void ImageViewingWindow::onNextImage()
{
    if (m_imageFiles.isEmpty()) return;
    m_zoomFactor = 1.0;
    m_currentIndex = (m_currentIndex + 1) % m_imageFiles.count();
    updateImageView();
}

void ImageViewingWindow::onOpenCurrentImage()
{
    if (!m_modeStack || m_imageFiles.isEmpty()) return;
    m_rotationAngle = 0;
    m_zoomFactor = 1.0;
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
    auto *central = new PageBgWidget(this);
    setCentralWidget(central);
    central->setStyleSheet("QWidget{color:#eaf2ff;}");

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
    homeBtn->setFocusPolicy(Qt::NoFocus);
    homeBtn->setStyleSheet(
        "QPushButton { border:none; background-image:url(:/images/pict_home_up.png); background-repeat:no-repeat; }"
        "QPushButton:hover { background-image:url(:/images/pict_home_down.png); }"
    );
    homeBtn->setCursor(Qt::PointingHandCursor);
    connect(homeBtn, &QPushButton::clicked, this, [this]() {
        emit requestReturnToMain();
        hide();   // ★ 不 close()，保留当前目录路径
    });

    m_titleLabel = new QLabel(QStringLiteral("图片浏览"), topBar);
    m_titleLabel->setGeometry(0, 0, 1280, 72);
    m_titleLabel->setStyleSheet("QLabel{color:#fff;font-size:36px;font-weight:700;background:transparent;}");
    m_titleLabel->setAlignment(Qt::AlignCenter);
    m_titleLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

    auto *topBarRight = new TopBarRightWidget(topBar);
    topBarRight->setGeometry(1280 - 16 - TopBarRightWidget::preferredWidth(), 17,
                             TopBarRightWidget::preferredWidth(), 48);

    auto *backBtn = new QPushButton(listPage);
    backBtn->setGeometry(60, 103, 60, 60);
    backBtn->setStyleSheet(
        "QPushButton { border:none; background:url(:/images/butt_back_up.png) no-repeat; }"
        "QPushButton:hover { background:url(:/images/butt_back_down.png) no-repeat; }"
    );
    backBtn->setFocusPolicy(Qt::NoFocus);
    backBtn->setCursor(Qt::PointingHandCursor);

    connect(backBtn, &QPushButton::clicked, this, &ImageViewingWindow::onBackDirClicked);

    auto *contentLayout = new QVBoxLayout(listPage);
    contentLayout->setContentsMargins(168, 190, 168, 36);
    contentLayout->setSpacing(44);

    m_thumbnailList = new QListWidget(listPage);
    m_thumbnailList->setViewMode(QListView::IconMode);
    m_thumbnailList->setMovement(QListView::Static);
    m_thumbnailList->setResizeMode(QListView::Adjust);
    m_thumbnailList->setWrapping(true);
    m_thumbnailList->setSpacing(0);
    m_thumbnailList->setIconSize(QSize(0, 0));        // 委托自己绘制，禁用内置图标
    m_thumbnailList->setGridSize(QSize(188, 178));
    m_thumbnailList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_thumbnailList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_thumbnailList->setFixedWidth(960);
    m_thumbnailList->setFixedHeight(356);
    m_thumbnailList->setItemDelegate(new ImageListItemDelegate(m_thumbnailList));
    m_thumbnailList->setStyleSheet(
        "QListWidget{border:none;background:transparent;outline:none;}"
        "QListWidget::item{border:none;color:#eaf3ff;font-size:20px;text-align:center;}"
        "QListWidget::item:selected{color:#00faff;}"
        "QListWidget::item:hover{color:#dff9ff;}"
    );
    m_thumbnailList->verticalScrollBar()->setStyleSheet(
        "QScrollBar:vertical{width:12px;background:transparent;border-radius:6px;margin:0; padding:0;}"
        "QScrollBar::groove:vertical{background:rgba(0,104,255,0.10);border-radius:3px;margin:0px 3px; padding:0;}"
        "QScrollBar::handle:vertical{background:#0068FF;border-radius:3px;min-height:60px;margin:3px 3px;}"
        "QScrollBar::handle:vertical:hover{background:#00faff;}"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical{height:0;background:none;border:none;}"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical{background:transparent;}"
    );

    m_detailLabel = new QLabel(listPage);
    m_detailLabel->setFixedHeight(50);
    m_detailLabel->setStyleSheet("QLabel{background:rgba(255,255,255,0.1);border:1px solid #0068ff;border-radius:5px;padding-left:24px;font-size:24px;color:#eaf4ff;}");

    contentLayout->addWidget(m_thumbnailList, 0);
    contentLayout->addWidget(m_detailLabel);
    contentLayout->addStretch(1);

    auto *viewPage = new QWidget(m_modeStack);
    viewPage->setStyleSheet("QWidget{background:#000;border:none;}");
    viewPage->setAttribute(Qt::WA_AcceptTouchEvents);
    viewPage->grabGesture(Qt::PinchGesture);
    viewPage->installEventFilter(this);
    m_viewPage = viewPage;

    m_previewLabel = new QLabel(viewPage);
    m_previewLabel->setGeometry(0, 0, 1280, 720);
    m_previewLabel->setAlignment(Qt::AlignCenter);
    m_previewLabel->setStyleSheet("QLabel{background:#000;}");

    // Back button directly on image — no semi-transparent parent
    auto *viewBack = new QPushButton(m_previewLabel);
    viewBack->setGeometry(12, 12, 48, 48);
    viewBack->setStyleSheet(
        "QPushButton{border:none;background:transparent;background-image:url(:/images/butt_video_back_up.png);background-repeat:no-repeat;background-position:center;}"
        "QPushButton:hover{background:transparent;background-image:url(:/images/butt_video_back_down.png);}"
    );
    viewBack->setFocusPolicy(Qt::NoFocus);
    connect(viewBack, &QPushButton::clicked, this, &ImageViewingWindow::onBackToList);

    // Title label carries its own semi-transparent bar background
    m_viewTitleLabel = new QLabel(QStringLiteral("图片"), m_previewLabel);
    m_viewTitleLabel->setGeometry(0, 0, 1280, 72);
    m_viewTitleLabel->setStyleSheet("QLabel{color:#fff;font-size:36px;font-weight:700;background:rgba(0,0,0,0.5);}");
    m_viewTitleLabel->setAlignment(Qt::AlignCenter);
    m_viewTitleLabel->lower();   // title bar behind the back button

    auto *bottomBar = new QWidget(m_previewLabel);
    bottomBar->setGeometry(0, 588, 1280, 132);
    bottomBar->setStyleSheet("background:rgba(0,0,0,0.5);");
    auto *btnLayout = new QHBoxLayout(bottomBar);
    btnLayout->setContentsMargins(0, 24, 0, 24);
    btnLayout->setSpacing(0);

    auto *btnWrap = new QWidget(bottomBar);
    btnWrap->setFixedWidth(420);
    btnWrap->setStyleSheet("background:transparent;");
    auto *btnWrapLay = new QHBoxLayout(btnWrap);
    btnWrapLay->setContentsMargins(0, 0, 0, 0);
    btnWrapLay->setSpacing(0);

    m_prevButton = new QPushButton(btnWrap);
    m_prevButton->setFixedSize(60, 60);
    m_prevButton->setStyleSheet(
        "QPushButton{border:none;background-color:transparent;background-image:url(:/images/butt_music_prev_up.png);background-repeat:no-repeat;background-position:center;}"
        "QPushButton:hover{background-color:transparent;background-image:url(:/images/butt_music_prev_down.png);background-repeat:no-repeat;}"
        "QPushButton:pressed{background-color:transparent;}"
    );
    connect(m_prevButton, &QPushButton::clicked, this, &ImageViewingWindow::onPrevImage);
    m_prevButton->setFocusPolicy(Qt::NoFocus);

    auto *hiddenCenter = new QPushButton(btnWrap);
    hiddenCenter->setFixedSize(84, 84);
    hiddenCenter->setVisible(false);

    m_nextButton = new QPushButton(btnWrap);
    m_nextButton->setFixedSize(60, 60);
    m_nextButton->setStyleSheet(
        "QPushButton{border:none;background-color:transparent;background-image:url(:/images/butt_music_next_up.png);background-repeat:no-repeat;background-position:center;}"
        "QPushButton:hover{background-color:transparent;background-image:url(:/images/butt_music_next_down.png);background-repeat:no-repeat;}"
        "QPushButton:pressed{background-color:transparent;}"
    );
    connect(m_nextButton, &QPushButton::clicked, this, &ImageViewingWindow::onNextImage);
    m_nextButton->setFocusPolicy(Qt::NoFocus);

    m_rotateButton = new QPushButton(btnWrap);
    m_rotateButton->setFixedSize(60, 60);
    m_rotateButton->setStyleSheet(
        "QPushButton{border:none;background-color:transparent;background-image:url(:/images/butt_image_rotate_up.png);background-repeat:no-repeat;background-position:center;}"
        "QPushButton:hover{background-color:transparent;background-image:url(:/images/butt_image_rotate_down.png);background-repeat:no-repeat;}"
        "QPushButton:pressed{background-color:transparent;}"
    );
    connect(m_rotateButton, &QPushButton::clicked, this, &ImageViewingWindow::onRotateImage);
    m_rotateButton->setFocusPolicy(Qt::NoFocus);

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

    connect(m_thumbnailList, &QListWidget::itemClicked, this, &ImageViewingWindow::onItemClicked);
    m_modeStack->setCurrentIndex(0);
}

void ImageViewingWindow::updateImageView()
{
    if (m_imageFiles.isEmpty() || m_currentIndex < 0 || m_currentIndex >= m_imageFiles.count())
        return;

    const QString filePath = m_imageFiles.at(m_currentIndex);
    const QFileInfo info(filePath);

    if (m_viewTitleLabel)
        m_viewTitleLabel->setText(info.fileName());

    if (!m_previewLabel) return;

    // Re-load from disk only when file or rotation changes (not on every zoom step)
    if (filePath != m_cachedImagePath || m_rotationAngle != m_cachedRotation) {
        QPixmap raw = QPixmap::fromImage(loadImageFile(filePath));
        if (raw.isNull())
            raw = QPixmap(QStringLiteral(":/images/image_view.png"));
        if (!raw.isNull() && m_rotationAngle != 0) {
            QTransform t;
            t.rotate(m_rotationAngle);
            raw = raw.transformed(t, Qt::SmoothTransformation);
        }
        m_cachedSourcePixmap = raw;
        m_cachedImagePath    = filePath;
        m_cachedRotation     = m_rotationAngle;
    }

    if (!m_cachedSourcePixmap.isNull()) {
        const Qt::TransformationMode mode =
            m_isPinching ? Qt::FastTransformation : Qt::SmoothTransformation;

        const double sw = m_cachedSourcePixmap.width();
        const double sh = m_cachedSourcePixmap.height();
        const double lw = m_previewLabel->width();
        const double lh = m_previewLabel->height();

        // Scale that fits the source into the label at zoom=1 (letterbox scale)
        const double fitScale    = qMin(lw / sw, lh / sh);
        const double displayScale = fitScale * m_zoomFactor;

        // Visible output size in label coords (capped at label dims)
        const int visW = qMin((int)qRound(sw * displayScale), (int)lw);
        const int visH = qMin((int)qRound(sh * displayScale), (int)lh);

        // Corresponding source crop region (always <= source size)
        // visW/displayScale == visH/displayScale maintains the label aspect,
        // so scaling with IgnoreAspectRatio produces no distortion.
        const int cropW = qBound(1, (int)qRound(visW / displayScale), (int)sw);
        const int cropH = qBound(1, (int)qRound(visH / displayScale), (int)sh);
        const int ox    = ((int)sw - cropW) / 2;
        const int oy    = ((int)sh - cropH) / 2;

        const QPixmap cropped = m_cachedSourcePixmap.copy(
            qMax(0, ox), qMax(0, oy), cropW, cropH);
        m_previewLabel->setPixmap(
            cropped.scaled(visW, visH, Qt::IgnoreAspectRatio, mode));
    }
}

void ImageViewingWindow::onItemClicked(QListWidgetItem *item)
{
    if (!item) return;
    const bool isDir = item->data(Qt::UserRole + 1).toBool();
    const QString path = item->data(Qt::UserRole).toString();
    if (isDir) {
        loadDirectory(path);
    } else {
        const int idx = m_imageFiles.indexOf(path);
        m_currentIndex = (idx >= 0) ? idx : 0;
        onOpenCurrentImage();
    }
}

void ImageViewingWindow::onThumbnailReady(const QString &path, const QImage &image)
{
    if (!m_thumbnailList) return;
    const QPixmap pix = QPixmap::fromImage(image);
    for (int i = 0; i < m_thumbnailList->count(); ++i) {
        QListWidgetItem *it = m_thumbnailList->item(i);
        if (it && it->data(Qt::UserRole).toString() == path) {
            it->setData(Qt::UserRole + 2, pix);
            m_thumbnailList->viewport()->update();
            break;
        }
    }
}

bool ImageViewingWindow::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == m_viewPage && event->type() == QEvent::Gesture) {
        auto *ge = static_cast<QGestureEvent *>(event);
        if (auto *pinch = static_cast<QPinchGesture *>(ge->gesture(Qt::PinchGesture))) {
            const Qt::GestureState state = pinch->state();
            if (state == Qt::GestureStarted || state == Qt::GestureUpdated) {
                m_isPinching = true;
                m_zoomFactor *= pinch->scaleFactor();
                m_zoomFactor  = qBound(0.5, m_zoomFactor, 10.0);
                updateImageView();
            } else if (state == Qt::GestureFinished) {
                m_isPinching = false;
                updateImageView();  // final smooth render
            } else if (state == Qt::GestureCanceled) {
                m_isPinching = false;
            }
            ge->accept(pinch);
            return true;
        }
    }
    return QMainWindow::eventFilter(obj, event);
}

static bool isUsbRootPath(const QString &root)
{
    return root.startsWith(kUsbMountPrefix);
}

static QString findFirstUsbImageDirectory()
{
    // 主路：QStorageInfo
    for (const QStorageInfo &storage : QStorageInfo::mountedVolumes()) {
        if (!storage.isValid() || !storage.isReady())
            continue;
        const QString root = storage.rootPath();
        if (isUsbRootPath(root))
            return root;
    }
    // 备用：直接扫文件系统
    QDir d(kUsbMountDir);
    if (d.exists()) {
        const QStringList subs = d.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        if (!subs.isEmpty()) return kUsbMountPrefix + subs.first();
    }
    return {};
}

void ImageViewingWindow::onBackDirClicked()
{
    if (m_modeStack && m_modeStack->currentIndex() == 1) {
        onBackToList();
        return;
    }

    // 前缀匹配确定 USB 根目录（sda1 才是设备根）
    QString deviceRoot;
    if (m_currentPath.startsWith(kUsbMountPrefix)) {
        int sep = m_currentPath.indexOf('/', 9);
        deviceRoot = (sep < 0) ? m_currentPath : m_currentPath.left(sep);
    }

    if (!deviceRoot.isEmpty()) {
        if (m_currentPath == deviceRoot) {
            emit requestReturnToMain();
            hide();
            return;
        }
        QDir dir(m_currentPath);
        if (dir.cdUp()) {
            const QString parent = dir.absolutePath();
            if (parent == deviceRoot || parent.startsWith(deviceRoot + '/')) {
                loadDirectory(parent);
                return;
            }
        }
        emit requestReturnToMain();
        hide();
        return;
    }

    // 非 USB 路径，直接返回主界面
    emit requestReturnToMain();
    hide();
}

void ImageViewingWindow::loadDirectory(const QString &path)
{
    QString normalizedPath = path;
    if (normalizedPath.isEmpty()
            || normalizedPath == kUsbMountDir
            || normalizedPath == QStringLiteral("/mnt")) {
        const QString usbPath = findFirstUsbImageDirectory();
        if (!usbPath.isEmpty()) {
            normalizedPath = usbPath;
        } else {
            m_currentPath.clear();
            m_imageFiles.clear();
            if (m_thumbnailList) {
                m_thumbnailList->clear();
            }
            if (m_detailLabel) {
                m_detailLabel->setText(QStringLiteral("请插入U盘"));
            }
            return;
        }
    }

    m_currentPath = normalizedPath;
    m_imageFiles.clear();
    if (!m_thumbnailList) return;

    m_thumbnailList->clear();

    QDir imgDir(normalizedPath);
    if (!imgDir.exists()) {
        if (!normalizedPath.startsWith(kUsbMountPrefix)) {
            m_currentPath.clear();
            if (m_detailLabel)
                m_detailLabel->setText(QStringLiteral("请插入U盘"));
            return;
        }
        // USB 路径目录不存在：继续走正常流程，标签在末尾正确显示
    }

    // 收集图片文件（供前后翻页）
    imgDir.setFilter(QDir::Files | QDir::NoDotAndDotDot);
    imgDir.setNameFilters(m_imageExtensions);
    imgDir.setSorting(QDir::Name);
    const QFileInfoList imgInfos = imgDir.entryInfoList();
    for (const QFileInfo &fi : imgInfos)
        m_imageFiles << fi.absoluteFilePath();

    // 先列目录
    QDir dirList(normalizedPath);
    dirList.setFilter(QDir::AllDirs | QDir::NoDotAndDotDot);
    dirList.setSorting(QDir::Name);
    const QIcon folderIcon(QStringLiteral(":/images/butt_driving_image_playback_folder_up.png"));
    const QIcon fileIcon(QStringLiteral(":/images/image_imagellist_up.png"));

    for (const QFileInfo &fi : dirList.entryInfoList()) {
        auto *it = new QListWidgetItem(fi.fileName(), m_thumbnailList);
        it->setData(Qt::UserRole, fi.absoluteFilePath());
        it->setData(Qt::UserRole + 1, true);
        it->setSizeHint(QSize(188, 178));
        it->setTextAlignment(Qt::AlignHCenter | Qt::AlignBottom);
    }
    // 再列图片文件
    for (const QFileInfo &fi : imgInfos) {
        auto *it = new QListWidgetItem(fi.baseName(), m_thumbnailList);
        it->setData(Qt::UserRole, fi.absoluteFilePath());
        it->setData(Qt::UserRole + 1, false);
        it->setSizeHint(QSize(188, 178));
        it->setTextAlignment(Qt::AlignHCenter | Qt::AlignBottom);
    }

    // Start async thumbnail loading
    if (m_thumbLoader && !m_imageFiles.isEmpty()) {
        m_thumbLoader->cancel();
        QMetaObject::invokeMethod(m_thumbLoader, "process",
            Qt::QueuedConnection, Q_ARG(QStringList, m_imageFiles));
    }

    if (m_detailLabel) {
        // sda1 才是设备根：取 USB 基础路径后第一级子目录
        QString deviceRoot;
        if (normalizedPath.startsWith(kUsbMountPrefix)) {
            int sep = normalizedPath.indexOf('/', 9);
            deviceRoot = (sep < 0) ? normalizedPath : normalizedPath.left(sep);
        }
        QString displayPath;
        if (!deviceRoot.isEmpty()) {
            displayPath = QStringLiteral("U盘");
            if (normalizedPath != deviceRoot) {
                QString rel = normalizedPath.mid(deviceRoot.length() + 1);
                if (!rel.isEmpty())
                    displayPath += QStringLiteral(" > ") + rel.replace('/', QStringLiteral(" > "));
            }
        } else {
            displayPath = normalizedPath;
        }
        if (m_thumbnailList && m_thumbnailList->count() == 0)
            displayPath = QStringLiteral("无内容");
        m_detailLabel->setText(displayPath);
    }
}

void ImageViewingWindow::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_VolumeUp:
        AppSignals::changeVolume(+1);
        break;
    case Qt::Key_VolumeDown:
        AppSignals::changeVolume(-1);
        break;
    case Qt::Key_MediaPrevious:
        onPrevImage();
        break;
    case Qt::Key_MediaNext:
        onNextImage();
        break;
    case Qt::Key_HomePage:
        emit requestReturnToMain();
        hide();   // ★ 不 close()，保留当前目录路径
        break;
    case Qt::Key_Back:
    case Qt::Key_Escape:
        // onBackDirClicked 已处理：查看页→列表页；子目录→上级；根目录→返回主界面
        onBackDirClicked();
        break;
    default:
        QMainWindow::keyPressEvent(event);
    }
}
