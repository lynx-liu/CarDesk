#include "drivingimageplaybackpage.h"

#include "ahdrecordstore.h"
#include "appsignals.h"
#include "drivingimagesubtopbar.h"

#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPushButton>
#include <QShowEvent>
#include <QSizePolicy>
#include <QTextDocument>
#include <QTextCursor>
#include <QTextOption>
#include <QTimer>
#include <QVBoxLayout>

namespace {

constexpr int kGridCols = 4;
constexpr int kGridRows = 2;
constexpr int kItemsPerPage = kGridCols * kGridRows;
constexpr int kTileW = 200;
constexpr int kTileH = 160;
constexpr int kGridW = 872;
constexpr int kGridH = 344;
constexpr int kGridGap = 24;

class PlaybackTileButton : public QPushButton {
public:
    PlaybackTileButton(const QString &text, const QString &upImage, const QString &downImage,
                       QWidget *parent = nullptr)
        : QPushButton(parent)
        , m_text(text)
        , m_upImage(upImage)
        , m_downImage(downImage)
    {
        setFixedSize(kTileW, kTileH);
        setFlat(true);
        setFocusPolicy(Qt::NoFocus);
        setAutoDefault(false);
        setDefault(false);
        setCheckable(false);
        setStyleSheet(QStringLiteral("QPushButton{border:none;background:transparent;outline:none;}"));
    }

    void clearPressedState()
    {
        m_pressed = false;
        setDown(false);
        update();
    }

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        QPushButton::mousePressEvent(event);
        if (event->button() == Qt::LeftButton) {
            m_pressed = true;
            update();
        }
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        QPushButton::mouseReleaseEvent(event);
        m_pressed = false;
        setDown(false);
        update();
    }

    void leaveEvent(QEvent *event) override
    {
        QPushButton::leaveEvent(event);
        m_pressed = false;
        setDown(false);
        update();
    }

    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, false);

        const bool highlight = m_pressed || underMouse();
        const QPixmap bg(highlight ? m_downImage : m_upImage);
        if (!bg.isNull()) {
            painter.drawPixmap(0, 0, width(), height(), bg);
        }

        QFont font = painter.font();
        font.setPixelSize(15);
        painter.setFont(font);
        painter.setPen(highlight ? QColor(0, 250, 255) : QColor(255, 255, 255));
        const QRect textRect(6, 110, width() - 12, height() - 110);

        QTextOption option(Qt::AlignHCenter | Qt::AlignVCenter);
        option.setWrapMode(QTextOption::WrapAnywhere);
        QTextDocument doc;
        doc.setDefaultFont(font);
        doc.setDefaultTextOption(option);
        doc.setTextWidth(textRect.width());
        doc.setPlainText(m_text);
        QTextCharFormat fmt;
        fmt.setForeground(highlight ? QColor(0, 250, 255) : QColor(255, 255, 255));
        QTextCursor cursor(&doc);
        cursor.select(QTextCursor::Document);
        cursor.mergeCharFormat(fmt);

        painter.save();
        painter.translate(textRect.left(), textRect.top());
        const qreal yOffset = qMax<qreal>(0, (textRect.height() - doc.size().height()) / 2.0);
        painter.translate(0, yOffset);
        doc.drawContents(&painter);
        painter.restore();
    }

private:
    QString m_text;
    QString m_upImage;
    QString m_downImage;
    bool m_pressed = false;
};

QWidget *createGridHost(QGridLayout **gridOut)
{
    auto *host = new QWidget;
    host->setFixedSize(kGridW, kGridH);
    host->setStyleSheet(QStringLiteral("background:transparent;border:none;"));
    auto *grid = new QGridLayout(host);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(kGridGap);
    grid->setVerticalSpacing(kGridGap);
    *gridOut = grid;
    return host;
}

void clearGrid(QGridLayout *grid)
{
    if (!grid) {
        return;
    }
    while (QLayoutItem *item = grid->takeAt(0)) {
        if (QWidget *widget = item->widget()) {
            widget->deleteLater();
        }
        delete item;
    }
}

void clearTilePressedStates(QGridLayout *grid)
{
    if (!grid) {
        return;
    }
    for (int i = 0; i < grid->count(); ++i) {
        if (auto *tile = dynamic_cast<PlaybackTileButton *>(grid->itemAt(i)->widget())) {
            tile->clearPressedState();
        }
    }
}

int indexOfRecordPath(const QStringList &files, const QString &path)
{
    if (path.isEmpty()) {
        return -1;
    }
    const QString anchor = QFileInfo(path).absoluteFilePath();
    for (int i = 0; i < files.size(); ++i) {
        if (QFileInfo(files.at(i)).absoluteFilePath() == anchor) {
            return i;
        }
    }
    return -1;
}

} // namespace

DrivingImagePlaybackPage::DrivingImagePlaybackPage(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("drivingImagePlaybackPage"));
    setStyleSheet(QStringLiteral("#drivingImagePlaybackPage{background:transparent;border:none;}"));
    setMinimumSize(0, 0);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    setupUI();

    m_recordRefreshDebounce = new QTimer(this);
    m_recordRefreshDebounce->setSingleShot(true);
    m_recordRefreshDebounce->setInterval(400);
    connect(m_recordRefreshDebounce, &QTimer::timeout, this,
            &DrivingImagePlaybackPage::refreshCurrentView);
    connect(AppSignals::instance(), &AppSignals::recordFilesChanged, this, [this]() {
        if (isVisible()) {
            m_recordRefreshDebounce->start();
        }
    });

    reloadFiles();
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

    auto *content = new QWidget(this);
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 48, 0, 0);
    contentLayout->setSpacing(0);

    m_emptyHint = new QLabel(QStringLiteral("暂无录像"), content);
    m_emptyHint->setAlignment(Qt::AlignCenter);
    m_emptyHint->setStyleSheet(
        QStringLiteral("color:#eaf2ff;font-size:28px;background:transparent;border:none;"));
    m_emptyHint->hide();

    auto *fileGridCenter = new QHBoxLayout();
    fileGridCenter->addStretch();
    m_fileGridHost = createGridHost(&m_fileGrid);
    fileGridCenter->addWidget(m_fileGridHost);
    fileGridCenter->addStretch();

    auto *listArea = new QVBoxLayout();
    listArea->setContentsMargins(0, 0, 0, 0);
    listArea->setSpacing(0);
    listArea->addWidget(m_emptyHint, 1);
    listArea->addLayout(fileGridCenter);

    auto *pageBtnWrap = new QWidget(content);
    pageBtnWrap->setFixedWidth(597);
    auto *pageBtnRow = new QHBoxLayout(pageBtnWrap);
    pageBtnRow->setContentsMargins(0, 36, 0, 0);
    pageBtnRow->setSpacing(0);
    m_prevPageBtn = new QPushButton(QStringLiteral("上一页"), pageBtnWrap);
    m_nextPageBtn = new QPushButton(QStringLiteral("下一页"), pageBtnWrap);
    m_prevPageBtn->setFocusPolicy(Qt::NoFocus);
    m_nextPageBtn->setFocusPolicy(Qt::NoFocus);
    const QString pageBtnStyle =
        QStringLiteral("QPushButton{background:none;color:#fff;font-size:24px;"
                       "border:2px solid #0068FF;min-width:136px;min-height:54px;outline:none;}"
                       "QPushButton:hover{color:#00FAFF;border:2px solid #00FAFF;}"
                       "QPushButton:disabled{color:#666666;border:2px solid #666666;}"
                       "QPushButton:focus,QPushButton:pressed{outline:none;}");
    m_prevPageBtn->setStyleSheet(pageBtnStyle);
    m_nextPageBtn->setStyleSheet(pageBtnStyle);
    pageBtnRow->addWidget(m_prevPageBtn);
    pageBtnRow->addStretch();
    pageBtnRow->addWidget(m_nextPageBtn);

    auto *pageBtnCenter = new QHBoxLayout();
    pageBtnCenter->addStretch();
    pageBtnCenter->addWidget(pageBtnWrap);
    pageBtnCenter->addStretch();

    contentLayout->addLayout(listArea, 1);
    contentLayout->addLayout(pageBtnCenter);
    root->addWidget(content, 1);

    connect(m_prevPageBtn, &QPushButton::clicked, this, [this]() {
        if (m_currentPage <= 0) {
            return;
        }
        --m_currentPage;
        populateFileGrid();
    });
    connect(m_nextPageBtn, &QPushButton::clicked, this, [this]() {
        const int maxPage = qMax(0, (m_allFiles.size() - 1) / kItemsPerPage);
        if (m_currentPage >= maxPage) {
            return;
        }
        ++m_currentPage;
        populateFileGrid();
    });
}

void DrivingImagePlaybackPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    m_launchingPlayback = false;
    clearTilePressedStates(m_fileGrid);
    if (m_skipRefreshOnNextShow) {
        m_skipRefreshOnNextShow = false;
        return;
    }
    refreshCurrentView();
}

void DrivingImagePlaybackPage::refreshCurrentView()
{
    const int page = m_currentPage;
    m_allFiles = AhdRecordStore::filterExistingFiles(
        AhdRecordStore::listAllVideoFilesOrdered());
    const int maxPage = m_allFiles.isEmpty()
        ? 0
        : qMax(0, (m_allFiles.size() - 1) / kItemsPerPage);
    m_currentPage = qMin(page, maxPage);
    if (m_emptyHint) {
        m_emptyHint->setVisible(m_allFiles.isEmpty());
    }
    if (m_fileGridHost) {
        m_fileGridHost->setVisible(!m_allFiles.isEmpty());
    }
    populateFileGrid();
}

void DrivingImagePlaybackPage::reloadFiles()
{
    m_currentPage = 0;
    refreshCurrentView();
}

void DrivingImagePlaybackPage::restoreAfterVideoPlayback(const QString &anchorPath)
{
    m_launchingPlayback = false;
    m_skipRefreshOnNextShow = true;
    m_allFiles = AhdRecordStore::filterExistingFiles(
        AhdRecordStore::listAllVideoFilesOrdered());
    const int maxPage = m_allFiles.isEmpty()
        ? 0
        : qMax(0, (m_allFiles.size() - 1) / kItemsPerPage);
    const int fileIndex = indexOfRecordPath(m_allFiles, anchorPath);
    m_currentPage = fileIndex >= 0 ? (fileIndex / kItemsPerPage) : 0;
    m_currentPage = qBound(0, m_currentPage, maxPage);
    if (m_emptyHint) {
        m_emptyHint->setVisible(m_allFiles.isEmpty());
    }
    if (m_fileGridHost) {
        m_fileGridHost->setVisible(!m_allFiles.isEmpty());
    }
    populateFileGrid();
}

void DrivingImagePlaybackPage::populateFileGrid()
{
    clearGrid(m_fileGrid);
    const int start = m_currentPage * kItemsPerPage;
    const int end = qMin(start + kItemsPerPage, m_allFiles.size());
    int slot = 0;
    for (int i = start; i < end; ++i) {
        const QString path = m_allFiles.at(i);
        auto *tile = new PlaybackTileButton(
            AhdRecordStore::displayNameForFile(path),
            QStringLiteral(":/images/butt_driving_image_playback_filelist_up.png"),
            QStringLiteral(":/images/butt_driving_image_playback_filelist_down.png"),
            m_fileGridHost);
        connect(tile, &QPushButton::released, this, [this, path]() { playFile(path); });
        m_fileGrid->addWidget(tile, slot / kGridCols, slot % kGridCols);
        ++slot;
    }
    updatePageButtons();
}

void DrivingImagePlaybackPage::updatePageButtons()
{
    const int maxPage = qMax(0, (m_allFiles.size() - 1) / kItemsPerPage);
    if (m_prevPageBtn) {
        m_prevPageBtn->setEnabled(m_currentPage > 0);
    }
    if (m_nextPageBtn) {
        m_nextPageBtn->setEnabled(m_currentPage < maxPage);
    }
}

void DrivingImagePlaybackPage::playFile(const QString &path)
{
    if (m_launchingPlayback) {
        return;
    }

    // 列表页已有文件列表；打开时只过滤失效项，避免再整树扫盘卡住 UI
    m_allFiles = AhdRecordStore::filterExistingFiles(m_allFiles);
    int index = m_allFiles.indexOf(path);
    if (index < 0 || !QFileInfo::exists(path)) {
        // 锚点失效时才补一次全量扫描
        m_allFiles = AhdRecordStore::filterExistingFiles(
            AhdRecordStore::listAllVideoFilesOrdered());
        index = m_allFiles.indexOf(path);
        if (m_allFiles.isEmpty() || index < 0 || !QFileInfo::exists(path)) {
            clearTilePressedStates(m_fileGrid);
            populateFileGrid();
            return;
        }
    }

    clearTilePressedStates(m_fileGrid);
    m_launchingPlayback = true;
    emit requestPlayVideo(m_allFiles, index);
}
