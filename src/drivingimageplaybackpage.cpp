#include "drivingimageplaybackpage.h"

#include "ahdrecordstore.h"
#include "drivingimagesubtopbar.h"
#include "videoplaywindow.h"

#include <QFileInfo>
#include <QHideEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QStackedWidget>
#include <QListView>
#include <QSizePolicy>
#include <QVBoxLayout>

DrivingImagePlaybackPage::DrivingImagePlaybackPage(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("drivingImagePlaybackPage"));
    setStyleSheet(QStringLiteral("#drivingImagePlaybackPage{background:transparent;border:none;}"));
    setMinimumSize(0, 0);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    setupUI();
    reloadDates();
}

DrivingImagePlaybackPage::~DrivingImagePlaybackPage()
{
    if (m_player) {
        m_player->deleteLater();
        m_player = nullptr;
    }
}

void DrivingImagePlaybackPage::setupUI()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 108);
    root->setSpacing(0);

    m_topBar = new DrivingImageSubTopBar(this);
    m_topBar->setTitle(QStringLiteral("行车影像"));
    connect(m_topBar, &DrivingImageSubTopBar::homeClicked, this,
            &DrivingImagePlaybackPage::requestReturnToMain);
    root->addWidget(m_topBar);

    m_backBtn = new QPushButton(this);
    m_backBtn->setGeometry(60, 103, 60, 60);
    m_backBtn->setFocusPolicy(Qt::NoFocus);
    m_backBtn->hide();
    m_backBtn->setStyleSheet(
        QStringLiteral("QPushButton{border:none;background-image:url(:/images/butt_back_up.png);"
                       "background-repeat:no-repeat;background-position:center;}"
                       "QPushButton:hover,QPushButton:pressed{"
                       "background-image:url(:/images/butt_back_down.png);}"));
    connect(m_backBtn, &QPushButton::clicked, this, [this]() { showDateList(); });

    auto *content = new QWidget(this);
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 48, 0, 0);
    contentLayout->setSpacing(0);

    m_stack = new QStackedWidget(content);
    m_stack->setStyleSheet(QStringLiteral("background:transparent;border:none;"));

    const QString folderListStyle =
        QStringLiteral("QListWidget{background:transparent;border:none;outline:none;}"
                       "QListWidget::item{color:#fff;font-size:18px;padding-top:117px;"
                       "text-align:center;background:transparent;}"
                       "QListWidget::item:selected{color:#00FAFF;}");

    auto *datePage = new QWidget(m_stack);
    auto *dateLayout = new QVBoxLayout(datePage);
    dateLayout->setContentsMargins(0, 0, 0, 0);

    m_emptyHint = new QLabel(QStringLiteral("暂无录像"), datePage);
    m_emptyHint->setAlignment(Qt::AlignCenter);
    m_emptyHint->setStyleSheet(
        QStringLiteral("color:#eaf2ff;font-size:28px;background:transparent;border:none;"));
    m_emptyHint->hide();

    m_dateList = new QListWidget(datePage);
    m_dateList->setViewMode(QListView::IconMode);
    m_dateList->setMovement(QListView::Static);
    m_dateList->setResizeMode(QListView::Adjust);
    m_dateList->setIconSize(QSize(200, 160));
    m_dateList->setGridSize(QSize(220, 180));
    m_dateList->setSpacing(12);
    m_dateList->setStyleSheet(folderListStyle);
    m_dateList->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_dateList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    dateLayout->addWidget(m_emptyHint, 1);
    dateLayout->addWidget(m_dateList, 1);

    auto *filePage = new QWidget(m_stack);
    auto *fileLayout = new QVBoxLayout(filePage);
    fileLayout->setContentsMargins(0, 0, 0, 0);
    m_fileList = new QListWidget(filePage);
    m_fileList->setViewMode(QListView::IconMode);
    m_fileList->setMovement(QListView::Static);
    m_fileList->setResizeMode(QListView::Adjust);
    m_fileList->setIconSize(QSize(200, 160));
    m_fileList->setGridSize(QSize(220, 180));
    m_fileList->setSpacing(12);
    m_fileList->setStyleSheet(
        QStringLiteral("QListWidget{background:transparent;border:none;outline:none;}"
                       "QListWidget::item{color:#fff;font-size:18px;padding-top:117px;"
                       "text-align:center;background:transparent;}"
                       "QListWidget::item:selected{color:#00FAFF;}"));
    fileLayout->addWidget(m_fileList, 1);

    m_datePageIndex = m_stack->addWidget(datePage);
    m_stack->addWidget(filePage);

    auto *pageBtnRow = new QHBoxLayout();
    pageBtnRow->setContentsMargins(0, 36, 0, 0);
    pageBtnRow->addStretch();
    m_prevPageBtn = new QPushButton(QStringLiteral("上一页"), content);
    m_nextPageBtn = new QPushButton(QStringLiteral("下一页"), content);
  const QString pageBtnStyle =
        QStringLiteral("QPushButton{background:transparent;color:#fff;font-size:24px;"
                       "border:2px solid #0068FF;min-width:136px;min-height:54px;}"
                       "QPushButton:hover{color:#00FAFF;border-color:#00FAFF;}");
    m_prevPageBtn->setStyleSheet(pageBtnStyle);
    m_nextPageBtn->setStyleSheet(pageBtnStyle);
    pageBtnRow->addWidget(m_prevPageBtn);
    pageBtnRow->addSpacing(325);
    pageBtnRow->addWidget(m_nextPageBtn);
    pageBtnRow->addStretch();

    contentLayout->addWidget(m_stack, 1);
    contentLayout->addLayout(pageBtnRow);
    root->addWidget(content, 1);

    connect(m_dateList, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        if (!item) {
            return;
        }
        showFileList(item->text());
    });

    connect(m_fileList, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        if (!item || item->text() == QStringLiteral("该日期无文件")) {
            return;
        }
        playFile(item->data(Qt::UserRole).toString());
    });
}

void DrivingImagePlaybackPage::reloadDates()
{
    m_dateList->clear();
    const QStringList dates = AhdRecordStore::listDateFolders();
    const QIcon folderIcon(QStringLiteral(":/images/butt_driving_image_playback_folder_up.png"));
    for (const QString &d : dates) {
        auto *item = new QListWidgetItem(folderIcon, d, m_dateList);
        item->setSizeHint(QSize(200, 160));
        item->setTextAlignment(Qt::AlignHCenter | Qt::AlignBottom);
    }
    const bool empty = dates.isEmpty();
    if (m_emptyHint) {
        m_emptyHint->setVisible(empty);
    }
    m_dateList->setVisible(!empty);
    showDateList();
}

void DrivingImagePlaybackPage::showDateList()
{
    m_stack->setCurrentIndex(m_datePageIndex);
    m_backBtn->hide();
}

void DrivingImagePlaybackPage::showFileList(const QString &dateKey)
{
    m_currentDate = dateKey;
    m_fileList->clear();
    const QStringList files = AhdRecordStore::listVideoFilesForDate(dateKey);
    const QIcon fileIcon(QStringLiteral(":/images/butt_driving_image_playback_filelist_up.png"));
    for (const QString &path : files) {
        auto *item = new QListWidgetItem(fileIcon, AhdRecordStore::displayNameForFile(path), m_fileList);
        item->setData(Qt::UserRole, path);
        item->setSizeHint(QSize(200, 160));
        item->setTextAlignment(Qt::AlignHCenter | Qt::AlignBottom);
    }
    if (files.isEmpty()) {
        m_fileList->addItem(QStringLiteral("该日期无文件"));
    }
    m_stack->setCurrentIndex(1);
    m_backBtn->show();
    m_backBtn->raise();
}

void DrivingImagePlaybackPage::playFile(const QString &path)
{
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        return;
    }
    if (!m_player) {
        m_player = new VideoPlayWindow();
        m_player->setAttribute(Qt::WA_DeleteOnClose);
        connect(m_player, &VideoPlayWindow::requestReturnToMain, this, [this]() {
            if (m_player) {
                m_player->hide();
            }
            show();
            raise();
        });
    }
    QStringList files = AhdRecordStore::listVideoFilesForDate(m_currentDate);
    int index = files.indexOf(path);
    if (index < 0) {
        files = QStringList(path);
        index = 0;
    }
    m_player->setVideoFiles(files, index);
    m_player->show();
    m_player->raise();
    m_player->activateWindow();
}

void DrivingImagePlaybackPage::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    if (m_player && m_player->isVisible()) {
        m_player->hide();
    }
}
