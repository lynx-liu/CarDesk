#include "imageviewingwindow.h"
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
#include <QSize>
#include <QStackedWidget>
#include <QTransform>
#include <QVBoxLayout>
#include <QApplication>
#include <QIcon>
#include <QScreen>
#include <QTime>
#include "appsignals.h"

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
    loadDirectory(m_initialPath);
}

void ImageViewingWindow::closeEvent(QCloseEvent *event)
{
    emit requestReturnToMain();
    QMainWindow::closeEvent(event);
}

void ImageViewingWindow::onPrevImage()
{
    if (m_imageFiles.isEmpty()) return;
    m_currentIndex = (m_currentIndex + m_imageFiles.count() - 1) % m_imageFiles.count();
    updateImageView();
}

void ImageViewingWindow::onNextImage()
{
    if (m_imageFiles.isEmpty()) return;
    m_currentIndex = (m_currentIndex + 1) % m_imageFiles.count();
    updateImageView();
}

void ImageViewingWindow::onOpenCurrentImage()
{
    if (!m_modeStack || m_imageFiles.isEmpty()) return;
    m_rotationAngle = 0;
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
    m_thumbnailList->setFixedHeight(356);
    m_thumbnailList->setItemDelegate(new ImageListItemDelegate(m_thumbnailList));
    m_thumbnailList->setStyleSheet(
        "QListWidget{border:none;background:transparent;outline:none;}"
        "QListWidget::item{border:none;color:#eaf3ff;font-size:20px;text-align:center;}"
        "QListWidget::item:selected{color:#00faff;}"
        "QListWidget::item:hover{color:#dff9ff;}"
    );

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
    viewBack->setFocusPolicy(Qt::NoFocus);
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
    m_prevButton->setFocusPolicy(Qt::NoFocus);

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
    m_nextButton->setFocusPolicy(Qt::NoFocus);

    m_rotateButton = new QPushButton(btnWrap);
    m_rotateButton->setFixedSize(60, 60);
    m_rotateButton->setStyleSheet(
        "QPushButton{border:none;background-image:url(:/images/butt_image_rotate_up.png);background-repeat:no-repeat;background-position:center;}"
        "QPushButton:hover{background-image:url(:/images/butt_image_rotate_down.png);}"
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

    if (m_previewLabel) {
        QPixmap source(filePath);
        if (source.isNull())
            source = QPixmap(QStringLiteral(":/images/image_view.png"));
        if (!source.isNull()) {
            if (m_rotationAngle != 0) {
                QTransform transform;
                transform.rotate(m_rotationAngle);
                source = source.transformed(transform, Qt::SmoothTransformation);
            }
            m_previewLabel->setPixmap(source.scaled(m_previewLabel->size(),
                Qt::KeepAspectRatio, Qt::SmoothTransformation));
        }
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

void ImageViewingWindow::onBackDirClicked()
{
    if (m_modeStack && m_modeStack->currentIndex() == 1) {
        onBackToList();
        return;
    }
    if (m_currentPath != m_initialPath) {
        QDir dir(m_currentPath);
        if (dir.cdUp())
            loadDirectory(dir.absolutePath());
    } else {
        emit requestReturnToMain();
        hide();
    }
}

void ImageViewingWindow::loadDirectory(const QString &path)
{
    m_currentPath = path;
    m_imageFiles.clear();
    if (!m_thumbnailList) return;

    m_thumbnailList->clear();

    // 收集图片文件（供前后翻页）
    QDir imgDir(path);
    imgDir.setFilter(QDir::Files | QDir::NoDotAndDotDot);
    imgDir.setNameFilters(m_imageExtensions);
    imgDir.setSorting(QDir::Name);
    const QFileInfoList imgInfos = imgDir.entryInfoList();
    for (const QFileInfo &fi : imgInfos)
        m_imageFiles << fi.absoluteFilePath();

    // 先列目录
    QDir dirList(path);
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

    if (m_detailLabel)
        m_detailLabel->setText(m_currentPath);
}

void ImageViewingWindow::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_VolumeUp:
        AppSignals::runAmixer({"sset", "LINEOUT volume", "5%+"}, this);
        break;
    case Qt::Key_VolumeDown:
        AppSignals::runAmixer({"sset", "LINEOUT volume", "5%-"}, this);
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
