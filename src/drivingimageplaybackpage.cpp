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
#include <QStackedWidget>
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

    reloadDates();
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

    auto *datePage = new QWidget(m_stack);
    auto *dateLayout = new QVBoxLayout(datePage);
    dateLayout->setContentsMargins(0, 0, 0, 0);
    dateLayout->setSpacing(0);

    m_emptyHint = new QLabel(QStringLiteral("暂无录像"), datePage);
    m_emptyHint->setAlignment(Qt::AlignCenter);
    m_emptyHint->setStyleSheet(
        QStringLiteral("color:#eaf2ff;font-size:28px;background:transparent;border:none;"));
    m_emptyHint->hide();

    auto *dateGridCenter = new QHBoxLayout();
    dateGridCenter->addStretch();
    m_dateGridHost = createGridHost(&m_dateGrid);
    dateGridCenter->addWidget(m_dateGridHost);
    dateGridCenter->addStretch();

    dateLayout->addWidget(m_emptyHint, 1);
    dateLayout->addLayout(dateGridCenter);

    auto *filePage = new QWidget(m_stack);
    auto *fileLayout = new QVBoxLayout(filePage);
    fileLayout->setContentsMargins(0, 0, 0, 0);
    fileLayout->setSpacing(0);
    auto *fileGridCenter = new QHBoxLayout();
    fileGridCenter->addStretch();
    m_fileGridHost = createGridHost(&m_fileGrid);
    fileGridCenter->addWidget(m_fileGridHost);
    fileGridCenter->addStretch();
    fileLayout->addLayout(fileGridCenter);

    m_datePageIndex = m_stack->addWidget(datePage);
    m_stack->addWidget(filePage);

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

    contentLayout->addWidget(m_stack, 1);
    contentLayout->addLayout(pageBtnCenter);
    root->addWidget(content, 1);

    connect(m_prevPageBtn, &QPushButton::clicked, this, [this]() {
        if (m_currentPage <= 0) {
            return;
        }
        --m_currentPage;
        if (m_showingFiles) {
            populateFileGrid();
        } else {
            populateDateGrid();
        }
    });
    connect(m_nextPageBtn, &QPushButton::clicked, this, [this]() {
        const int total = m_showingFiles ? m_allFiles.size() : m_allDates.size();
        const int maxPage = qMax(0, (total - 1) / kItemsPerPage);
        if (m_currentPage >= maxPage) {
            return;
        }
        ++m_currentPage;
        if (m_showingFiles) {
            populateFileGrid();
        } else {
            populateDateGrid();
        }
    });
}

void DrivingImagePlaybackPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    m_launchingPlayback = false;
    clearTilePressedStates(m_fileGrid);
    clearTilePressedStates(m_dateGrid);
    if (m_skipRefreshOnNextShow) {
        m_skipRefreshOnNextShow = false;
        return;
    }
    refreshCurrentView();
}

void DrivingImagePlaybackPage::refreshCurrentView()
{
    if (m_showingFiles && !m_currentDate.isEmpty()) {
        const int page = m_currentPage;
        m_allFiles = AhdRecordStore::filterExistingFiles(
            AhdRecordStore::listVideoFilesForDate(m_currentDate));
        const int maxPage = m_allFiles.isEmpty()
            ? 0
            : qMax(0, (m_allFiles.size() - 1) / kItemsPerPage);
        m_currentPage = qMin(page, maxPage);
        if (m_emptyHint) {
            m_emptyHint->setVisible(m_allFiles.isEmpty());
        }
        populateFileGrid();
        return;
    }
    reloadDates();
}

void DrivingImagePlaybackPage::reloadDates()
{
    m_allDates = AhdRecordStore::listDateFolders();
    m_currentPage = 0;
    m_showingFiles = false;
    const bool empty = m_allDates.isEmpty();
    if (m_emptyHint) {
        m_emptyHint->setVisible(empty);
    }
    if (m_dateGridHost) {
        m_dateGridHost->setVisible(!empty);
    }
    populateDateGrid();
    showDateList();
}

void DrivingImagePlaybackPage::restoreAfterVideoPlayback(const QString &anchorPath)
{
    m_launchingPlayback = false;
    if (!m_currentDate.isEmpty()) {
        m_skipRefreshOnNextShow = true;
        showFileList(m_currentDate, anchorPath);
        return;
    }
    reloadDates();
}

void DrivingImagePlaybackPage::populateDateGrid()
{
    clearGrid(m_dateGrid);
    const int start = m_currentPage * kItemsPerPage;
    const int end = qMin(start + kItemsPerPage, m_allDates.size());
    int slot = 0;
    for (int i = start; i < end; ++i) {
        const QString &dateKey = m_allDates.at(i);
        auto *tile = new PlaybackTileButton(
            dateKey,
            QStringLiteral(":/images/butt_driving_image_playback_folder_up.png"),
            QStringLiteral(":/images/butt_driving_image_playback_folder_down.png"),
            m_dateGridHost);
        connect(tile, &QPushButton::released, this, [this, dateKey]() { showFileList(dateKey); });
        m_dateGrid->addWidget(tile, slot / kGridCols, slot % kGridCols);
        ++slot;
    }
    updatePageButtons();
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
    const int total = m_showingFiles ? m_allFiles.size() : m_allDates.size();
    const int maxPage = qMax(0, (total - 1) / kItemsPerPage);
    if (m_prevPageBtn) {
        m_prevPageBtn->setEnabled(m_currentPage > 0);
    }
    if (m_nextPageBtn) {
        m_nextPageBtn->setEnabled(m_currentPage < maxPage);
    }
}

void DrivingImagePlaybackPage::showDateList()
{
    m_showingFiles = false;
    m_currentPage = 0;
    m_stack->setCurrentIndex(m_datePageIndex);
    m_backBtn->hide();
    populateDateGrid();
}

void DrivingImagePlaybackPage::showFileList(const QString &dateKey, const QString &anchorPath)
{
    m_currentDate = dateKey;
    m_allFiles = AhdRecordStore::filterExistingFiles(AhdRecordStore::listVideoFilesForDate(dateKey));
    m_showingFiles = true;

    const int maxPage = m_allFiles.isEmpty()
        ? 0
        : qMax(0, (m_allFiles.size() - 1) / kItemsPerPage);
    const int fileIndex = indexOfRecordPath(m_allFiles, anchorPath);
    m_currentPage = fileIndex >= 0 ? (fileIndex / kItemsPerPage) : 0;
    m_currentPage = qBound(0, m_currentPage, maxPage);

    populateFileGrid();
    m_stack->setCurrentIndex(1);
    m_backBtn->show();
    m_backBtn->raise();
}

void DrivingImagePlaybackPage::playFile(const QString &path)
{
    if (m_launchingPlayback) {
        return;
    }

    m_allFiles = AhdRecordStore::filterExistingFiles(AhdRecordStore::listVideoFilesForDate(m_currentDate));
    if (m_allFiles.isEmpty()) {
        populateFileGrid();
        return;
    }

    int index = m_allFiles.indexOf(path);
    if (index < 0 || !QFileInfo::exists(path)) {
        clearTilePressedStates(m_fileGrid);
        populateFileGrid();
        return;
    }

    clearTilePressedStates(m_fileGrid);
    m_launchingPlayback = true;
    emit requestPlayVideo(m_allFiles, index);
}
