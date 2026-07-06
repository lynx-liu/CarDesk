#include "diagnosticwindow.h"
#include "mupdfdocument.h"
#include "pagebgwidget.h"
#include "devicedetect.h"
#include "faultcodedb.h"
#include "mcuserialreader.h"
#include "topbarwidget.h"
#include "appsignals.h"
#include "pinyin_dictionary.h"

#include <QApplication>
#include <QKeyEvent>
#include <QProcess>
#include <QCloseEvent>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QTime>
#include <QVBoxLayout>
#include <QDir>
#include <QPainter>
#include <QRegExp>
#include <QFileInfo>
#include <QListWidget>
#include <QSharedPointer>
#include <QListWidgetItem>
#include <algorithm>

DiagnosticWindow::DiagnosticWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_pages(nullptr)
    , m_pdfHeaderLabel(nullptr)
    , m_pdfSearchKeywordLabel(nullptr)
    , m_pdfSearchResultLabel(nullptr)
    , m_pdfRenderLabel(nullptr)
    , m_searchInput(nullptr)
    , m_jumpInput(nullptr)
    , m_pdfBottomNormal(nullptr)
    , m_pdfBottomSearch(nullptr)
    , m_currentPdfFilePath()
    , m_pdfDocument(new MuPdfDocument())
    , m_pdfPage(1)
    , m_pdfTotal(10)
    , m_resultIndex(1)
    , m_resultTotal(8)
    , m_reader(McuSerialReader::ensureShared())
    , m_faultBadgeLabels{}
    , m_faultDetailTitleLabel(nullptr)
    , m_faultDetailScrollArea(nullptr)
{
    setWindowTitle(QStringLiteral("诊断维护"));
    setObjectName("diagnosticWindow");
    setFixedSize(1280, 720);

    const DeviceDetect &device = DeviceDetect::instance();
    if (device.getDeviceType() == DeviceDetect::DEVICE_TYPE_CARUNIT) {
        setWindowState(Qt::WindowFullScreen);
    } else if (QApplication::primaryScreen()) {
        move(QApplication::primaryScreen()->geometry().center() - rect().center());
    }

    setupUI();

    connect(m_reader, &McuSerialReader::dm1Received,
            this, &DiagnosticWindow::onFaultDataReceived);
    if (device.getDeviceType() == DeviceDetect::DEVICE_TYPE_CARUNIT)
        m_reader->open(QStringLiteral("/dev/ttyS2"));
}

DiagnosticWindow::~DiagnosticWindow()
{
    delete m_pdfDocument;
    m_pdfDocument = nullptr;
}

void DiagnosticWindow::closeEvent(QCloseEvent *event)
{
    emit requestReturnToMain();
    QMainWindow::closeEvent(event);
}

void DiagnosticWindow::setupUI()
{
    auto *central = new PageBgWidget(this);
    setCentralWidget(central);
    central->setStyleSheet("QWidget{color:#eaf2ff;}");

    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_pages = new QStackedWidget(central);
    m_pages->setStyleSheet("QStackedWidget{background:transparent;border:none;}");
    m_pages->addWidget(createMainMenuPage());
    m_pages->addWidget(createFaultPage());
    m_pages->addWidget(createMaintenanceBookPage());
    m_pages->addWidget(createPdfPage());
    m_pages->addWidget(createPdfSearchPage());
    m_pages->addWidget(createPdfJumpPage());
    m_pages->addWidget(createFaultDetailPage());  // index 6

    root->addWidget(m_pages, 1);
    openPage(0);
}

QWidget *DiagnosticWindow::createMainMenuPage()
{
    auto *page = new QWidget();
    page->setStyleSheet("QWidget{background:transparent;}");

    auto *topBar = new QWidget(page);
    topBar->setGeometry(0, 0, 1280, 82);
    topBar->setStyleSheet("background: url(:/images/topbar.png) no-repeat;");

    auto *homeBtn = new QPushButton(topBar);
    homeBtn->setGeometry(12, 12, 48, 48);
    homeBtn->setCursor(Qt::PointingHandCursor);
    homeBtn->setStyleSheet(
        "QPushButton{border:none;background-image:url(:/images/pict_home_up.png);background-repeat:no-repeat;}"
        "QPushButton:hover{background-image:url(:/images/pict_home_down.png);}"
    );
    connect(homeBtn, &QPushButton::clicked, this, [this]() {
        emit requestReturnToMain();
        hide();
    });

    auto *title = new QLabel(QStringLiteral("诊断维护"), topBar);
    title->setGeometry(0, 0, 1280, 72);
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet("QLabel{color:#fff;font-size:36px;font-weight:700;background:transparent;}");
    title->setAttribute(Qt::WA_TransparentForMouseEvents);
    homeBtn->raise();

    auto *topBarRight = new TopBarRightWidget(topBar);
    topBarRight->setGeometry(1280 - 16 - TopBarRightWidget::preferredWidth(), 17,
                             TopBarRightWidget::preferredWidth(), 48);

    auto *menuWrap = new QWidget(page);
    // CSS .diagnostic_maintenance_con { margin-top:146px } => y = topbar(82) + 146 = 228
    menuWrap->setGeometry(0, 228, 1280, 420);
    auto *menuLayout = new QHBoxLayout(menuWrap);
    menuLayout->setContentsMargins(408, 0, 408, 0);
    menuLayout->setSpacing(80);

    auto *faultBtn = new QPushButton(QStringLiteral("故障诊断"), menuWrap);
    faultBtn->setCursor(Qt::PointingHandCursor);
    faultBtn->setFixedSize(192, 250);
    faultBtn->setStyleSheet(
        "QPushButton{border:none;background:url(:/images/butt_diagnosis_fault_up.png) no-repeat top center;padding-top:208px;"
        "font-size:24px;color:#fff;}"
        "QPushButton:hover{background-image:url(:/images/butt_diagnosis_fault_down.png);}"
    );
    connect(faultBtn, &QPushButton::clicked, this, &DiagnosticWindow::onOpenFaultPage);

    auto *maintainBtn = new QPushButton(QStringLiteral("使用维护"), menuWrap);
    maintainBtn->setCursor(Qt::PointingHandCursor);
    maintainBtn->setFixedSize(192, 250);
    maintainBtn->setStyleSheet(
        "QPushButton{border:none;background:url(:/images/butt_diagnosis_maintenance_up.png) no-repeat top center;padding-top:208px;"
        "font-size:24px;color:#fff;}"
        "QPushButton:hover{background-image:url(:/images/butt_diagnosis_maintenance_down.png);}"
    );
    connect(maintainBtn, &QPushButton::clicked, this, &DiagnosticWindow::onOpenMaintenanceBookPage);

    menuLayout->addWidget(faultBtn);
    menuLayout->addWidget(maintainBtn);

    return page;
}

QWidget *DiagnosticWindow::createFaultPage()
{
    auto *page = new QWidget();
    page->setStyleSheet("QWidget{background:transparent;}");

    auto *topBar = new QWidget(page);
    topBar->setGeometry(0, 0, 1280, 82);
    topBar->setStyleSheet("background: url(:/images/topbar.png) no-repeat;");

    auto *homeBtn = new QPushButton(topBar);
    homeBtn->setGeometry(12, 12, 48, 48);
    homeBtn->setCursor(Qt::PointingHandCursor);
    homeBtn->setStyleSheet(
        "QPushButton{border:none;background-image:url(:/images/pict_home_up.png);background-repeat:no-repeat;}"
        "QPushButton:hover{background-image:url(:/images/pict_home_down.png);}"
    );
    connect(homeBtn, &QPushButton::clicked, this, [this]() {
        emit requestReturnToMain();
        hide();
    });

    auto *title = new QLabel(QStringLiteral("故障诊断"), topBar);
    title->setGeometry(0, 0, 1280, 72);
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet("QLabel{color:#fff;font-size:36px;font-weight:700;background:transparent;}");
    title->setAttribute(Qt::WA_TransparentForMouseEvents);
    homeBtn->raise();

    auto *topBarRight = new TopBarRightWidget(topBar);
    topBarRight->setGeometry(1280 - 16 - TopBarRightWidget::preferredWidth(), 17,
                             TopBarRightWidget::preferredWidth(), 48);

    auto *backBtn = new QPushButton(page);
    backBtn->setGeometry(60, 103, 60, 60);
    backBtn->setCursor(Qt::PointingHandCursor);
    backBtn->setStyleSheet(
        "QPushButton{border:none;background:url(:/images/butt_back_up.png) no-repeat;}"
        "QPushButton:hover{background:url(:/images/butt_back_down.png) no-repeat;}"
    );
    connect(backBtn, &QPushButton::clicked, this, &DiagnosticWindow::onBackFromTopLevelSubPage);

    const QStringList names = {
        QStringLiteral("ABS系统"),
        QStringLiteral("双预警系统"),
        QStringLiteral("车身控制器")
    };
    const QStringList up = {
        QStringLiteral(":/images/butt_diagnosis_abs_up.png"),
        QStringLiteral(":/images/butt_diagnosis_warning_up.png"),
        QStringLiteral(":/images/butt_diagnosis_controller_up.png")
    };
    const QStringList down = {
        QStringLiteral(":/images/butt_diagnosis_abs_down.png"),
        QStringLiteral(":/images/butt_diagnosis_warning_down.png"),
        QStringLiteral(":/images/butt_diagnosis_controller_down.png")
    };

    const QStringList ctrlKeys = {
        QStringLiteral("ABS"),
        QStringLiteral("EBS"),
        QStringLiteral("BCM")
    };
    for (int i = 0; i < names.size(); ++i) {
        auto *btn = new QPushButton(names.at(i), page);
        // CSS .diagnostic_fault_con { margin-top:207px } => y = 82+207 = 289
        // CSS ul { justify-content:center } li { width:124; margin:0 74px }
        // => x_start = (1280 - 3*272)/2 = 232, item_x = 232+74 = 306, step = 272
        btn->setGeometry(306 + i * 272, 289, 124, 140);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet(
            QString(
                "QPushButton{border:none;background:url(%1) no-repeat top center;padding-top:108px;font-size:24px;color:#fff;text-align:center;}"
                "QPushButton:hover{background-image:url(%2);}"
            ).arg(up.at(i), down.at(i))
        );
        const QString ctrl = ctrlKeys.at(i);
        connect(btn, &QPushButton::clicked, this, [this, ctrl]() {
            showFaultDetail(ctrl);
        });
        // 故障徽标（有故障时显示）
        m_faultBadgeLabels[i] = new QLabel(btn);
        m_faultBadgeLabels[i]->setGeometry(77, -8, 32, 32);
        m_faultBadgeLabels[i]->setAlignment(Qt::AlignCenter);
        m_faultBadgeLabels[i]->setStyleSheet(
            "QLabel{background:#B82F2F;color:#fff;font-size:24px;border-radius:16px;}");
        m_faultBadgeLabels[i]->hide();
    }

    return page;
}

QWidget *DiagnosticWindow::createMaintenanceBookPage()
{
    auto *page = new QWidget();
    page->setStyleSheet("QWidget{background:transparent;}");

    auto *topBar = new QWidget(page);
    topBar->setGeometry(0, 0, 1280, 82);
    topBar->setStyleSheet("background: url(:/images/topbar.png) no-repeat;");

    auto *homeBtn = new QPushButton(topBar);
    homeBtn->setGeometry(12, 12, 48, 48);
    homeBtn->setCursor(Qt::PointingHandCursor);
    homeBtn->setStyleSheet(
        "QPushButton{border:none;background-image:url(:/images/pict_home_up.png);background-repeat:no-repeat;}"
        "QPushButton:hover{background-image:url(:/images/pict_home_down.png);}"
    );
    connect(homeBtn, &QPushButton::clicked, this, [this]() {
        emit requestReturnToMain();
        hide();
    });

    auto *title = new QLabel(QStringLiteral("诊断维护"), topBar);
    title->setGeometry(0, 0, 1280, 72);
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet("QLabel{color:#fff;font-size:36px;font-weight:700;background:transparent;}");
    title->setAttribute(Qt::WA_TransparentForMouseEvents);
    homeBtn->raise();

    auto *topBarRight = new TopBarRightWidget(topBar);
    topBarRight->setGeometry(1280 - 16 - TopBarRightWidget::preferredWidth(), 17,
                             TopBarRightWidget::preferredWidth(), 48);

    auto *backBtn = new QPushButton(page);
    backBtn->setGeometry(60, 103, 60, 60);
    backBtn->setCursor(Qt::PointingHandCursor);
    backBtn->setStyleSheet(
        "QPushButton{border:none;background:url(:/images/butt_back_up.png) no-repeat;}"
        "QPushButton:hover{background:url(:/images/butt_back_down.png) no-repeat;}"
    );
    connect(backBtn, &QPushButton::clicked, this, &DiagnosticWindow::onBackFromTopLevelSubPage);

    auto *bookWrap = new QWidget(page);
    // CSS .diagnostic_maintenance_book { width:944; margin:24px auto }
    // => x=(1280-944)/2=168, y=topbar(82)+24=106
    bookWrap->setGeometry(168, 106, 944, 558);
    auto *bookLayout = new QVBoxLayout(bookWrap);
    bookLayout->setContentsMargins(0, 0, 0, 0);
    bookLayout->setSpacing(0);

    const QDir docDir(QStringLiteral("/usr/share/doc"));
    QFileInfoList entries = docDir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot,
                                                  QDir::Name | QDir::DirsFirst);
    if (entries.isEmpty()) {
        auto *hint = new QLabel(QStringLiteral("未找到维护资料文件。\n请检查 /usr/share/doc 目录是否存在或是否包含文件。"), bookWrap);
        hint->setAlignment(Qt::AlignCenter);
        hint->setWordWrap(true);
        hint->setStyleSheet("QLabel{color:#aaa;font-size:28px;background:transparent;}");
        bookLayout->addStretch();
        bookLayout->addWidget(hint);
        bookLayout->addStretch();
    } else {
        auto *list = new QListWidget(bookWrap);
        list->setFrameShape(QFrame::NoFrame);
        list->setFocusPolicy(Qt::NoFocus);
        list->setStyleSheet(
            "QListWidget{background:transparent;border:none;color:#fff;font-size:24px;outline:none;}"
            "QListWidget::item{height:68px;background:rgba(255,255,255,0.1);margin:0 0 2px 0;padding:0 24px;text-align:left;outline:none;}"
            "QListWidget::item:hover{background:rgba(0,104,255,0.10);color:#dff9ff;}"
            "QListWidget::item:selected{background:rgba(0,104,255,0.35);color:#fff;outline:none;}"
        );
        list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        list->setContentsMargins(0, 0, 0, 0);
        list->setSpacing(0);

        std::sort(entries.begin(), entries.end(), [](const QFileInfo &a, const QFileInfo &b) {
            const auto parseNumericPrefix = [](const QString &name, int &num, QString &rest) {
                int i = 0;
                while (i < name.size() && name.at(i).isDigit())
                    ++i;
                if (i == 0)
                    return false;
                bool ok = false;
                num = name.left(i).toInt(&ok);
                if (!ok)
                    return false;
                rest = name.mid(i);
                return true;
            };
            int aNum = 0, bNum = 0;
            QString aRest, bRest;
            const bool aHas = parseNumericPrefix(a.fileName(), aNum, aRest);
            const bool bHas = parseNumericPrefix(b.fileName(), bNum, bRest);
            if (aHas && bHas) {
                if (aNum != bNum)
                    return aNum < bNum;
                return aRest.compare(bRest, Qt::CaseInsensitive) < 0;
            }
            if (aHas != bHas)
                return aHas;
            return a.fileName().compare(b.fileName(), Qt::CaseInsensitive) < 0;
        });
        for (const QFileInfo &info : entries) {
            QListWidgetItem *item = new QListWidgetItem(info.fileName(), list);
            item->setData(Qt::UserRole, info.absoluteFilePath());
            item->setSizeHint(QSize(944, 68));
        }
        list->verticalScrollBar()->setStyleSheet(
            "QScrollBar:vertical{width:48px;background:transparent;border-radius:6px;margin:0;padding:0;}"
            "QScrollBar::groove:vertical{background:rgba(0,104,255,0.10);border-radius:3px;margin:0px 18px;padding:0;}"
            "QScrollBar::handle:vertical{background:#0068FF;border-radius:3px;min-height:60px;margin:3px 18px;}"
            "QScrollBar::handle:vertical:hover{background:#00faff;}"
            "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical{height:0;background:none;border:none;}"
            "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical{background:transparent;}"
        );
        connect(list, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
            if (!item) {
                return;
            }
            m_currentPdfFilePath = item->data(Qt::UserRole).toString();
            onOpenPdfView();
        });
        bookLayout->addWidget(list);
    }

    return page;
}

QWidget *DiagnosticWindow::createPdfPage()
{
    auto *page = new QWidget();
    page->setStyleSheet("QWidget{background:#525659;}");

    auto *paper = new QLabel(page);
    paper->setGeometry(0, 0, 1280, 720);
    paper->setStyleSheet("QLabel{background:transparent;}");

    m_pdfRenderLabel = new QLabel(paper);
    m_pdfRenderLabel->setGeometry(0, 0, 1280, 720);
    m_pdfRenderLabel->setAlignment(Qt::AlignCenter);
    m_pdfRenderLabel->setStyleSheet("QLabel{background:transparent;}");
    m_pdfRenderLabel->setPixmap(QPixmap(":/images/pdf_view.png").scaled(1280, 720, Qt::KeepAspectRatio, Qt::SmoothTransformation));

    auto *topOverlay = new QWidget(page);
    topOverlay->setGeometry(0, 0, 1280, 72);
    topOverlay->setStyleSheet("background:rgba(0,0,0,0.5);");

    auto *backBtn = new QPushButton(topOverlay);
    backBtn->setGeometry(12, 12, 48, 48);
    backBtn->setCursor(Qt::PointingHandCursor);
    backBtn->setStyleSheet(
        "QPushButton{border:none;background:transparent url(:/images/butt_video_back_up.png) no-repeat center center;outline:none;}"
        "QPushButton:hover{background:transparent url(:/images/butt_video_back_down.png) no-repeat center center;outline:none;}"
        "QPushButton:pressed{background:transparent url(:/images/butt_video_back_down.png) no-repeat center center;outline:none;border:none;}"
        "QPushButton:focus{outline:none;border:none;}"
    );
    connect(backBtn, &QPushButton::clicked, this, [this]() { openPage(2); });

    auto *title = new QLabel(QStringLiteral("使用维护"), topOverlay);
    title->setGeometry(100, 0, 980, 72);
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet("QLabel{color:#fff;font-size:36px;background:transparent;}");
    title->setAttribute(Qt::WA_TransparentForMouseEvents);

    m_pdfHeaderLabel = new QLabel(topOverlay);
    m_pdfHeaderLabel->setGeometry(1148, 18, 120, 36);
    m_pdfHeaderLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_pdfHeaderLabel->setStyleSheet("QLabel{color:#fff;font-size:24px;background:transparent;}");

    auto *jumpBtn = new QPushButton(topOverlay);
    jumpBtn->setGeometry(1148, 18, 120, 36);
    jumpBtn->setCursor(Qt::PointingHandCursor);
    jumpBtn->setStyleSheet("QPushButton{border:none;background:transparent;}");
    connect(jumpBtn, &QPushButton::clicked, this, &DiagnosticWindow::onOpenPdfJumpPage);

    m_pdfBottomNormal = new QWidget(page);
    m_pdfBottomNormal->setGeometry(0, 588, 1280, 132);
    m_pdfBottomNormal->setStyleSheet("background:rgba(0,0,0,0.5);");
    auto *btnLayout = new QHBoxLayout(m_pdfBottomNormal);
    btnLayout->setContentsMargins(430, 24, 430, 24);
    btnLayout->setSpacing(24);

    auto *prevBtn = new QPushButton(m_pdfBottomNormal);
    prevBtn->setFixedSize(60, 60);
    prevBtn->setCursor(Qt::PointingHandCursor);
    prevBtn->setStyleSheet(
        "QPushButton{border:none;background:transparent url(:/images/butt_music_prev_up.png) no-repeat center center;outline:none;}"
        "QPushButton:hover{background:transparent url(:/images/butt_music_prev_down.png) no-repeat center center;outline:none;}"
        "QPushButton:pressed{background:transparent url(:/images/butt_music_prev_down.png) no-repeat center center;outline:none;border:none;}"
        "QPushButton:focus{outline:none;border:none;}"
    );
    connect(prevBtn, &QPushButton::clicked, this, &DiagnosticWindow::onPrevPage);

    auto *nextBtn = new QPushButton(m_pdfBottomNormal);
    nextBtn->setFixedSize(60, 60);
    nextBtn->setCursor(Qt::PointingHandCursor);
    nextBtn->setStyleSheet(
        "QPushButton{border:none;background:transparent url(:/images/butt_music_next_up.png) no-repeat center center;outline:none;}"
        "QPushButton:hover{background:transparent url(:/images/butt_music_next_down.png) no-repeat center center;outline:none;}"
        "QPushButton:pressed{background:transparent url(:/images/butt_music_next_down.png) no-repeat center center;outline:none;border:none;}"
        "QPushButton:focus{outline:none;border:none;}"
    );
    connect(nextBtn, &QPushButton::clicked, this, &DiagnosticWindow::onNextPage);

    auto *searchBtn = new QPushButton(m_pdfBottomNormal);
    searchBtn->setFixedSize(60, 60);
    searchBtn->setCursor(Qt::PointingHandCursor);
    searchBtn->setStyleSheet(
        "QPushButton{border:none;background:transparent url(:/images/butt_radio_search_up.png) no-repeat center center;outline:none;}"
        "QPushButton:hover{background:transparent url(:/images/butt_radio_search_down.png) no-repeat center center;outline:none;}"
        "QPushButton:pressed{background:transparent url(:/images/butt_radio_search_down.png) no-repeat center center;outline:none;border:none;}"
        "QPushButton:focus{outline:none;}"
    );
    connect(searchBtn, &QPushButton::clicked, this, &DiagnosticWindow::onOpenPdfSearchPage);

    btnLayout->addWidget(prevBtn);
    btnLayout->addStretch(1);
    btnLayout->addWidget(nextBtn);
    btnLayout->addStretch(1);
    btnLayout->addWidget(searchBtn);

    m_pdfBottomSearch = new QWidget(page);
    m_pdfBottomSearch->setGeometry(0, 588, 1280, 132);
    m_pdfBottomSearch->setStyleSheet("background:rgba(0,0,0,0.5);");
    auto *searchLayout = new QHBoxLayout(m_pdfBottomSearch);
    searchLayout->setContentsMargins(30, 30, 30, 30);
    searchLayout->setSpacing(24);

    auto *closeResultBtn = new QPushButton(m_pdfBottomSearch);
    closeResultBtn->setFixedSize(48, 48);
    closeResultBtn->setCursor(Qt::PointingHandCursor);
    closeResultBtn->setStyleSheet(
        "QPushButton{border:none;background:transparent;background-image:url(:/images/butt_radio_search_del_all_up.png);background-repeat:no-repeat;outline:none;}"
        "QPushButton:hover{background:transparent;background-image:url(:/images/butt_radio_search_del_all_down.png);outline:none;}"
        "QPushButton:pressed{background:transparent;outline:none;}"
        "QPushButton:focus{outline:none;}"
    );
    connect(closeResultBtn, &QPushButton::clicked, this, [this]() {
        resetPdfSearchState();
    });

    m_pdfSearchKeywordLabel = new QLabel(QStringLiteral("维修"), m_pdfBottomSearch);
    m_pdfSearchKeywordLabel->setFixedSize(816, 72);
    m_pdfSearchKeywordLabel->setStyleSheet(
        "QLabel{border:1px solid #0068FF;color:#fff;font-size:48px;padding:0 24px;background:transparent;}"
    );

    auto *resultWrap = new QWidget(m_pdfBottomSearch);
    auto *resultLayout = new QHBoxLayout(resultWrap);
    resultLayout->setContentsMargins(0, 0, 0, 0);
    resultLayout->setSpacing(12);

    auto *prevResult = new QPushButton(resultWrap);
    prevResult->setFixedSize(48, 72);
    prevResult->setCursor(Qt::PointingHandCursor);
    prevResult->setStyleSheet(
        "QPushButton{border:none;background:transparent;background-image:url(:/images/butt_pdf_serch_result_pre_up.png);background-repeat:no-repeat;outline:none;}"
        "QPushButton:hover{background:transparent;background-image:url(:/images/butt_pdf_serch_result_pre_down.png);background-repeat:no-repeat;outline:none;}"
        "QPushButton:pressed{background:transparent;outline:none;}"
        "QPushButton:focus{outline:none;}"
    );
    connect(prevResult, &QPushButton::clicked, this, &DiagnosticWindow::onPrevSearchResult);

    m_pdfSearchResultLabel = new QLabel(resultWrap);
    m_pdfSearchResultLabel->setStyleSheet("QLabel{color:#fff;font-size:48px;background:transparent;}");

    auto *nextResult = new QPushButton(resultWrap);
    nextResult->setFixedSize(48, 72);
    nextResult->setCursor(Qt::PointingHandCursor);
    nextResult->setStyleSheet(
        "QPushButton{border:none;background:transparent;background-image:url(:/images/butt_pdf_serch_result_next_up.png);background-repeat:no-repeat;outline:none;}"
        "QPushButton:hover{background:transparent;background-image:url(:/images/butt_pdf_serch_result_next_down.png);background-repeat:no-repeat;outline:none;}"
        "QPushButton:pressed{background:transparent;outline:none;}"
        "QPushButton:focus{outline:none;}"
    );
    connect(nextResult, &QPushButton::clicked, this, &DiagnosticWindow::onNextSearchResult);

    resultLayout->addWidget(prevResult);
    resultLayout->addWidget(m_pdfSearchResultLabel);
    resultLayout->addWidget(nextResult);

    searchLayout->addWidget(closeResultBtn);
    searchLayout->addWidget(m_pdfSearchKeywordLabel);
    searchLayout->addWidget(resultWrap);

    m_pdfBottomSearch->hide();

    updatePdfHeader();
    updateSearchResultHeader();
    return page;
}

QWidget *DiagnosticWindow::createPdfSearchPage()
{
    auto *page = new QWidget();
    page->setStyleSheet("QWidget{background:transparent;}");

    auto *topBar = new QWidget(page);
    topBar->setGeometry(0, 0, 1280, 82);
    topBar->setStyleSheet("background: url(:/images/topbar.png) no-repeat;");

    auto *homeBtn = new QPushButton(topBar);
    homeBtn->setGeometry(12, 12, 48, 48);
    homeBtn->setCursor(Qt::PointingHandCursor);
    homeBtn->setStyleSheet(
        "QPushButton{border:none;background-image:url(:/images/pict_home_up.png);background-repeat:no-repeat;}"
        "QPushButton:hover{background-image:url(:/images/pict_home_down.png);}"
    );
    connect(homeBtn, &QPushButton::clicked, this, [this]() {
        emit requestReturnToMain();
        hide();
    });

    auto *title = new QLabel(QStringLiteral("诊断维护"), topBar);
    title->setGeometry(0, 0, 1280, 72);
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet("QLabel{color:#fff;font-size:36px;font-weight:700;background:transparent;}");
    title->setAttribute(Qt::WA_TransparentForMouseEvents);
    homeBtn->raise();

    auto *topBarRight = new TopBarRightWidget(topBar);
    topBarRight->setGeometry(1280 - 16 - TopBarRightWidget::preferredWidth(), 17,
                             TopBarRightWidget::preferredWidth(), 48);

    auto *backBtn = new QPushButton(page);
    backBtn->setGeometry(60, 103, 60, 60);
    backBtn->setCursor(Qt::PointingHandCursor);
    backBtn->setStyleSheet(
        "QPushButton{border:none;background:url(:/images/butt_back_up.png) no-repeat;}"
        "QPushButton:hover{background:url(:/images/butt_back_down.png) no-repeat;}"
    );
    connect(backBtn, &QPushButton::clicked, this, [this]() { openPage(3); });

    auto *inputWrap = new QWidget(page);
    inputWrap->setGeometry(172, 124, 1004, 72);
    inputWrap->setStyleSheet("QWidget{border:1px solid #0068FF;background:rgba(255,255,255,0.1);}");

    m_searchInput = new QLineEdit(inputWrap);
    m_searchInput->setGeometry(0, 0, 1004, 72);
    m_searchInput->setStyleSheet(
        "QLineEdit{border:none;background:transparent;color:#fff;font-size:48px;padding:0 24px;}"
        "QLineEdit::selection{background:transparent;color:#fff;text-decoration:underline;}");
    m_searchInput->setText(QString());

    auto *candidatePane = new QWidget(page);
    candidatePane->setGeometry(172, 196, 1004, 76);
    candidatePane->setStyleSheet("QWidget{border:1px solid #0068FF;background:rgba(0,0,0,0.15);}");
    auto *candidateLayout = new QHBoxLayout(candidatePane);
    candidateLayout->setContentsMargins(8, 0, 8, 0);
    candidateLayout->setSpacing(4);
    candidateLayout->setAlignment(Qt::AlignVCenter);

    struct PdfSearchState {
        QStringList currentCandidates;
        int currentCandidatePage = 0;
        QVector<QPushButton *> candidateButtons;
    };
    auto state = QSharedPointer<PdfSearchState>::create();
    auto compositionLength = QSharedPointer<int>::create(0);
    auto shiftMode = QSharedPointer<bool>::create(false);
    auto punctuationButtons = QSharedPointer<QVector<QPushButton *>>::create();
    const QStringList punctuationCn = {
        QStringLiteral("，"), QStringLiteral("。"), QStringLiteral("？"), QStringLiteral("！"),
        QStringLiteral("；"), QStringLiteral("："), QStringLiteral("（"), QStringLiteral("）"),
        QStringLiteral("《"), QStringLiteral("》")
    };
    const QStringList punctuationEn = {
        QStringLiteral(","), QStringLiteral("."), QStringLiteral("?"), QStringLiteral("!"),
        QStringLiteral(";"), QStringLiteral(":"), QStringLiteral("("), QStringLiteral(")"),
        QStringLiteral("<"), QStringLiteral(">")
    };
    const int candidatesPerPage = 10;

    for (int i = 0; i < candidatesPerPage; ++i) {
        auto *btn = new QPushButton(QString(), candidatePane);
        btn->setFixedSize(60, 64);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet("QPushButton{border:none;border-right:1px solid #0068FF;background:transparent;color:#fff;font-size:24px;padding:0 8px;}QPushButton:hover{color:#00FAFF;}");
        btn->hide();
        candidateLayout->addWidget(btn);
        state->candidateButtons.append(btn);
    }

    const QString pagerButtonStyle = "QPushButton{border:none;outline:none;background:transparent;color:#fff;font-size:20px;padding:0;margin:0;}QPushButton:hover{color:#00FAFF;}QPushButton:pressed{background:transparent;border:none;}QPushButton:focus{outline:none;border:none;}";
    auto *prevCandidatePage = new QPushButton(QStringLiteral("▲"), candidatePane);
    prevCandidatePage->setFixedSize(28, 28);
    prevCandidatePage->setCursor(Qt::PointingHandCursor);
    prevCandidatePage->setFlat(true);
    prevCandidatePage->setFocusPolicy(Qt::NoFocus);
    prevCandidatePage->setStyleSheet(pagerButtonStyle);
    auto *nextCandidatePage = new QPushButton(QStringLiteral("▼"), candidatePane);
    nextCandidatePage->setFixedSize(28, 28);
    nextCandidatePage->setCursor(Qt::PointingHandCursor);
    nextCandidatePage->setFlat(true);
    nextCandidatePage->setFocusPolicy(Qt::NoFocus);
    nextCandidatePage->setStyleSheet(pagerButtonStyle);
    auto *candidatePageLabel = new QLabel(QStringLiteral("1/1"), candidatePane);
    candidatePageLabel->setFixedSize(48, 28);
    candidatePageLabel->setAlignment(Qt::AlignCenter);
    candidatePageLabel->setStyleSheet("QLabel{color:#fff;font-size:20px;background:transparent;border:none;}");

    auto *pageLineLeft = new QFrame(candidatePane);
    pageLineLeft->setFrameShape(QFrame::VLine);
    pageLineLeft->setFrameShadow(QFrame::Plain);
    pageLineLeft->setLineWidth(1);
    pageLineLeft->setMidLineWidth(0);
    pageLineLeft->setFixedSize(1, 64);
    pageLineLeft->setStyleSheet("background:#0068FF;border:none;");

    auto *pageLineRight = new QFrame(candidatePane);
    pageLineRight->setFrameShape(QFrame::VLine);
    pageLineRight->setFrameShadow(QFrame::Plain);
    pageLineRight->setLineWidth(1);
    pageLineRight->setMidLineWidth(0);
    pageLineRight->setFixedSize(1, 64);
    pageLineRight->setStyleSheet("background:#0068FF;border:none;");

    auto *pageLabelWrap = new QWidget(candidatePane);
    pageLabelWrap->setFixedHeight(64);
    pageLabelWrap->setStyleSheet("background:transparent;border:none;");
    auto *pageLabelLayout = new QHBoxLayout(pageLabelWrap);
    pageLabelLayout->setContentsMargins(0, 0, 0, 0);
    pageLabelLayout->setSpacing(6);
    pageLabelLayout->setAlignment(Qt::AlignVCenter);
    pageLabelLayout->addWidget(pageLineLeft, 0, Qt::AlignVCenter);
    pageLabelLayout->addWidget(candidatePageLabel, 0, Qt::AlignVCenter);
    pageLabelLayout->addWidget(pageLineRight, 0, Qt::AlignVCenter);

    auto *pagerWrap = new QWidget(candidatePane);
    pagerWrap->setStyleSheet("background:transparent;border:none;");
    auto *pagerLayout = new QVBoxLayout(pagerWrap);
    pagerLayout->setContentsMargins(0, 0, 0, 0);
    pagerLayout->setSpacing(2);
    pagerLayout->addStretch(1);
    pagerLayout->addWidget(prevCandidatePage, 0, Qt::AlignHCenter);
    pagerLayout->addWidget(nextCandidatePage, 0, Qt::AlignHCenter);
    pagerLayout->addStretch(1);

    auto *pagerOuterWrap = new QWidget(candidatePane);
    pagerOuterWrap->setStyleSheet("background:transparent;border:none;");
    auto *pagerOuterLayout = new QHBoxLayout(pagerOuterWrap);
    pagerOuterLayout->setContentsMargins(0, 0, 0, 0);
    pagerOuterLayout->setSpacing(6);
    pagerOuterLayout->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    pagerOuterLayout->addWidget(pageLabelWrap, 0, Qt::AlignVCenter);
    pagerOuterLayout->addWidget(pagerWrap, 0, Qt::AlignVCenter);
    pagerOuterWrap->setFixedWidth(104);

    candidateLayout->addStretch(1);
    candidateLayout->addWidget(pagerOuterWrap, 0, Qt::AlignRight);

    const auto currentComposition = [this, compositionLength]() {
        if (*compositionLength <= 0) {
            return QString();
        }
        const int pos = m_searchInput->cursorPosition();
        const int startPos = pos - *compositionLength;
        if (startPos < 0) {
            return QString();
        }
        return m_searchInput->text().mid(startPos, *compositionLength);
    };

    const auto updateCompositionSelection = [this, compositionLength]() {
        if (*compositionLength > 0) {
            const int pos = m_searchInput->cursorPosition();
            const int startPos = pos - *compositionLength;
            if (startPos >= 0) {
                m_searchInput->setSelection(startPos, *compositionLength);
                return;
            }
        }
        m_searchInput->deselect();
    };

    const QString candidateButtonStyle =
        "QPushButton{border:none;border-right:1px solid #0068FF;background:transparent;color:#fff;font-size:24px; padding:0 8px;}"
        "QPushButton:hover{color:#00FAFF;}";
    const QString candidateButtonLastStyle =
        "QPushButton{border:none;background:transparent;color:#fff;font-size:24px; padding:0 8px;}"
        "QPushButton:hover{color:#00FAFF;}";

    const auto refreshCandidateDisplay = [state, candidatePageLabel, prevCandidatePage, nextCandidatePage, candidateButtonStyle, candidateButtonLastStyle, candidatesPerPage, updateCompositionSelection](const QString &pinyin) {
        const QString normalized = QString(pinyin).remove(QRegExp("\\d")).toLower();
        state->currentCandidates = pinyin.isEmpty() ? QStringList() : PinyinDictionary::candidates(normalized);
        const int pageCount = qMax(1, (state->currentCandidates.size() + candidatesPerPage - 1) / candidatesPerPage);
        state->currentCandidatePage = qBound(0, state->currentCandidatePage, pageCount - 1);
        const int firstIndex = state->currentCandidatePage * candidatesPerPage;
        const int visibleCount = qMax(0, qMin(state->currentCandidates.size() - firstIndex, state->candidateButtons.size()));

        for (int i = 0; i < state->candidateButtons.size(); ++i) {
            const int index = firstIndex + i;
            if (index < state->currentCandidates.size()) {
                state->candidateButtons[i]->setText(state->currentCandidates.at(index));
                state->candidateButtons[i]->setEnabled(true);
                state->candidateButtons[i]->setStyleSheet(i + 1 == visibleCount ? candidateButtonLastStyle : candidateButtonStyle);
                state->candidateButtons[i]->show();
            } else {
                state->candidateButtons[i]->hide();
            }
        }

        candidatePageLabel->setText(QStringLiteral("%1/%2").arg(state->currentCandidatePage + 1).arg(pageCount));
        candidatePageLabel->setFixedWidth(72);
        candidatePageLabel->setAlignment(Qt::AlignCenter);
        prevCandidatePage->setEnabled(state->currentCandidatePage > 0);
        nextCandidatePage->setEnabled(state->currentCandidatePage + 1 < pageCount);
        updateCompositionSelection();
    };

    for (auto *btn : qAsConst(state->candidateButtons)) {
        connect(btn, &QPushButton::clicked, this, [this, btn, refreshCandidateDisplay, compositionLength, updateCompositionSelection](void) {
            const QString candidate = btn->text();
            if (candidate.isEmpty() || *compositionLength <= 0) {
                return;
            }
            const int pos = m_searchInput->cursorPosition();
            const int startPos = pos - *compositionLength;
            QString mainText = m_searchInput->text();
            mainText.replace(startPos, *compositionLength, candidate);
            m_searchInput->setText(mainText);
            m_searchInput->setCursorPosition(startPos + candidate.length());
            m_searchInput->setFocus();
            *compositionLength = 0;
            refreshCandidateDisplay(QString());
            updateCompositionSelection();
        });
    }

    connect(prevCandidatePage, &QPushButton::clicked, this, [refreshCandidateDisplay, state, currentComposition](void) {
        --state->currentCandidatePage;
        refreshCandidateDisplay(currentComposition());
    });

    connect(nextCandidatePage, &QPushButton::clicked, this, [refreshCandidateDisplay, state, currentComposition](void) {
        ++state->currentCandidatePage;
        refreshCandidateDisplay(currentComposition());
    });

    auto *keyPadWrap = new QWidget(page);
    keyPadWrap->setGeometry(172, 304, 1004, 320);
    auto *grid = new QGridLayout(keyPadWrap);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(6);
    grid->setVerticalSpacing(6);

    const QStringList keys = {
        QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("3"), QStringLiteral("4"), QStringLiteral("5"), QStringLiteral("6"), QStringLiteral("7"), QStringLiteral("8"), QStringLiteral("9"), QStringLiteral("0"),
        QStringLiteral("Q"), QStringLiteral("W"), QStringLiteral("E"), QStringLiteral("R"), QStringLiteral("T"), QStringLiteral("Y"), QStringLiteral("U"), QStringLiteral("I"), QStringLiteral("O"), QStringLiteral("P"),
        QStringLiteral("A"), QStringLiteral("S"), QStringLiteral("D"), QStringLiteral("F"), QStringLiteral("G"), QStringLiteral("H"), QStringLiteral("J"), QStringLiteral("K"), QStringLiteral("L")
    };
    const QStringList bottomKeys = {
        QStringLiteral("Z"), QStringLiteral("X"), QStringLiteral("C"), QStringLiteral("V"), QStringLiteral("B"), QStringLiteral("N"), QStringLiteral("M"),
        QStringLiteral("Shift"), QStringLiteral("←"), QStringLiteral("空格")
    };

    const QString normalKeyStyle =
        "QPushButton{border:1px solid #0068FF;background:rgba(255,255,255,0.1);color:#fff;font-size:26px;outline:none;}"
        "QPushButton:hover{border-color:#00FAFF;color:#00FAFF;}"
        "QPushButton:pressed{background:rgba(255,255,255,0.1);border-color:#0068FF;color:#fff;}"
        "QPushButton:focus{background:rgba(255,255,255,0.1);border-color:#0068FF;color:#fff;}";
    const QString shiftOnStyle =
        "QPushButton{border:1px solid #00FAFF;background:rgba(0,170,255,0.2);color:#fff;font-size:26px;outline:none;}"
        "QPushButton:hover{border-color:#00FAFF;color:#00FAFF;}"
        "QPushButton:pressed{background:rgba(0,170,255,0.2);border-color:#00FAFF;color:#fff;}"
        "QPushButton:focus{background:rgba(0,170,255,0.2);border-color:#00FAFF;color:#fff;}";

    const auto insertTextAtCursor = [this](const QString &text) {
        const int pos = m_searchInput->cursorPosition();
        QString existing = m_searchInput->text();
        existing.insert(pos, text);
        m_searchInput->setText(existing);
        m_searchInput->setCursorPosition(pos + text.length());
        m_searchInput->setFocus();
    };

    for (int i = 0; i < keys.size(); ++i) {
        auto *btn = new QPushButton(keys.at(i), keyPadWrap);
        btn->setFixedSize(95, 54);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFlat(true);
        btn->setAutoDefault(false);
        btn->setDefault(false);
        btn->setFocusPolicy(Qt::NoFocus);
        btn->setStyleSheet(normalKeyStyle);

        connect(btn, &QPushButton::clicked, this, [this, btn, refreshCandidateDisplay, compositionLength, shiftMode, currentComposition, punctuationButtons, punctuationCn, punctuationEn, insertTextAtCursor, normalKeyStyle, shiftOnStyle, bottomKeys, updateCompositionSelection](void) {
            const QString key = btn->text();
            if (key == QStringLiteral("Shift")) {
                *shiftMode = !*shiftMode;
                if (*shiftMode) {
                    btn->setStyleSheet(shiftOnStyle);
                } else {
                    btn->setStyleSheet(normalKeyStyle);
                    btn->clearFocus();
                }
                for (QPushButton *pun : *punctuationButtons) {
                    const int index = punctuationButtons->indexOf(pun);
                    if (index >= 0 && index < punctuationCn.size()) {
                        pun->setText(*shiftMode ? punctuationEn.at(index) : punctuationCn.at(index));
                    }
                }
                return;
            }

            if (key == QStringLiteral("←")) {
                const int pos = m_searchInput->cursorPosition();
                if (pos > 0) {
                    QString text = m_searchInput->text();
                    text.remove(pos - 1, 1);
                    m_searchInput->setText(text);
                    m_searchInput->setCursorPosition(pos - 1);
                    m_searchInput->setFocus();
                    if (*compositionLength > 0) {
                        if (pos - 1 >= pos - *compositionLength) {
                            --*compositionLength;
                        }
                        if (*compositionLength <= 0) {
                            *compositionLength = 0;
                        }
                        refreshCandidateDisplay(currentComposition());
                    }
                }
                return;
            }

            if (key == QStringLiteral("空格")) {
                if (*compositionLength > 0) {
                    *compositionLength = 0;
                    refreshCandidateDisplay(QString());
                    updateCompositionSelection();
                    return;
                }
                insertTextAtCursor(QStringLiteral(" "));
                return;
            }

            if (key.size() == 1 && key.at(0).isDigit()) {
                if (*compositionLength > 0) {
                    *compositionLength = 0;
                    refreshCandidateDisplay(QString());
                    updateCompositionSelection();
                }
                insertTextAtCursor(key);
                return;
            }

            if (key.size() == 1 && key.at(0).isLetter()) {
                if (*shiftMode) {
                    if (*compositionLength > 0) {
                        *compositionLength = 0;
                        refreshCandidateDisplay(QString());
                    }
                    insertTextAtCursor(key.toUpper());
                    return;
                }
                if (*compositionLength == 0) {
                    *compositionLength = 1;
                } else {
                    ++*compositionLength;
                }
                insertTextAtCursor(key.toLower());
                refreshCandidateDisplay(currentComposition());
                return;
            }
        });

        if (i < 10) {
            grid->addWidget(btn, 0, i);
        } else if (i < 20) {
            grid->addWidget(btn, 1, i - 10);
        } else {
            grid->addWidget(btn, 2, i - 20);
        }
    }

    auto *confirmKey = new QPushButton(QStringLiteral("确认"), keyPadWrap);
    confirmKey->setFixedSize(95, 54);
    confirmKey->setCursor(Qt::PointingHandCursor);
    confirmKey->setStyleSheet(normalKeyStyle);
    connect(confirmKey, &QPushButton::clicked, this, &DiagnosticWindow::onConfirmPdfSearch);
    grid->addWidget(confirmKey, 2, 9);

    for (int i = 0; i < bottomKeys.size(); ++i) {
        auto *btn = new QPushButton(bottomKeys.at(i), keyPadWrap);
        btn->setFixedSize(95, 54);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFlat(true);
        btn->setAutoDefault(false);
        btn->setDefault(false);
        btn->setFocusPolicy(Qt::NoFocus);
        btn->setStyleSheet(normalKeyStyle);
        connect(btn, &QPushButton::clicked, this, [this, btn, refreshCandidateDisplay, compositionLength, shiftMode, currentComposition, punctuationButtons, punctuationCn, punctuationEn, insertTextAtCursor, normalKeyStyle, shiftOnStyle, bottomKeys, updateCompositionSelection](void) {
            const QString key = btn->text();
            if (key == QStringLiteral("Shift")) {
                *shiftMode = !*shiftMode;
                if (*shiftMode) {
                    btn->setStyleSheet(shiftOnStyle);
                } else {
                    btn->setStyleSheet(normalKeyStyle);
                    btn->clearFocus();
                }
                for (QPushButton *pun : *punctuationButtons) {
                    const int index = punctuationButtons->indexOf(pun);
                    if (index >= 0 && index < punctuationCn.size()) {
                        pun->setText(*shiftMode ? punctuationEn.at(index) : punctuationCn.at(index));
                    }
                }
                return;
            }
            if (key == QStringLiteral("←")) {
                const int pos = m_searchInput->cursorPosition();
                if (pos > 0) {
                    QString text = m_searchInput->text();
                    text.remove(pos - 1, 1);
                    m_searchInput->setText(text);
                    m_searchInput->setCursorPosition(pos - 1);
                    m_searchInput->setFocus();
                    if (*compositionLength > 0) {
                        if (pos - 1 >= pos - *compositionLength) {
                            --*compositionLength;
                        }
                        if (*compositionLength <= 0) {
                            *compositionLength = 0;
                        }
                        refreshCandidateDisplay(currentComposition());
                        updateCompositionSelection();
                    }
                }
                return;
            }
            if (key == QStringLiteral("空格")) {
                if (*compositionLength > 0) {
                    *compositionLength = 0;
                    refreshCandidateDisplay(QString());
                    updateCompositionSelection();
                    btn->setStyleSheet(normalKeyStyle);
                    btn->clearFocus();
                    return;
                }
                insertTextAtCursor(QStringLiteral(" "));
                btn->setStyleSheet(normalKeyStyle);
                btn->clearFocus();
                return;
            }

            if (key.size() == 1 && key.at(0).isLetter()) {
                if (*shiftMode) {
                    if (*compositionLength > 0) {
                        *compositionLength = 0;
                        refreshCandidateDisplay(QString());
                        updateCompositionSelection();
                    }
                    insertTextAtCursor(key.toUpper());
                    btn->setStyleSheet(normalKeyStyle);
                    btn->clearFocus();
                    return;
                }
                if (*compositionLength == 0) {
                    *compositionLength = 1;
                } else {
                    ++*compositionLength;
                }
                insertTextAtCursor(key.toLower());
                refreshCandidateDisplay(currentComposition());
                updateCompositionSelection();
                btn->setStyleSheet(normalKeyStyle);
                btn->clearFocus();
                return;
            }
        });
        grid->addWidget(btn, 3, i);
    }

    for (int i = 0; i < punctuationCn.size(); ++i) {
        auto *btn = new QPushButton(punctuationCn.at(i), keyPadWrap);
        btn->setFixedSize(95, 54);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFlat(true);
        btn->setAutoDefault(false);
        btn->setDefault(false);
        btn->setFocusPolicy(Qt::NoFocus);
        btn->setStyleSheet(normalKeyStyle);
        punctuationButtons->append(btn);
        connect(btn, &QPushButton::clicked, this, [this, btn, refreshCandidateDisplay, compositionLength, currentComposition, insertTextAtCursor, updateCompositionSelection](void) {
            if (*compositionLength > 0) {
                *compositionLength = 0;
                refreshCandidateDisplay(QString());
                updateCompositionSelection();
            }
            insertTextAtCursor(btn->text());
        });
        grid->addWidget(btn, 4, i);
    }

    refreshCandidateDisplay(QString());

    m_searchInput->setFocus();
    m_searchInput->setCursorPosition(m_searchInput->text().length());
    return page;
}

QWidget *DiagnosticWindow::createPdfJumpPage()
{
    auto *page = new QWidget();
    page->setStyleSheet("QWidget{background:transparent;}");

    auto *topBar = new QWidget(page);
    topBar->setGeometry(0, 0, 1280, 82);
    topBar->setStyleSheet("background: url(:/images/topbar.png) no-repeat;");

    auto *homeBtn = new QPushButton(topBar);
    homeBtn->setGeometry(12, 12, 48, 48);
    homeBtn->setCursor(Qt::PointingHandCursor);
    homeBtn->setStyleSheet(
        "QPushButton{border:none;background-image:url(:/images/pict_home_up.png);background-repeat:no-repeat;}"
        "QPushButton:hover{background-image:url(:/images/pict_home_down.png);}"
    );
    connect(homeBtn, &QPushButton::clicked, this, [this]() {
        emit requestReturnToMain();
        hide();
    });

    auto *title = new QLabel(QStringLiteral("诊断维护"), topBar);
    title->setGeometry(0, 0, 1280, 72);
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet("QLabel{color:#fff;font-size:36px;font-weight:700;background:transparent;}");
    title->setAttribute(Qt::WA_TransparentForMouseEvents);
    homeBtn->raise();

    auto *topBarRight = new TopBarRightWidget(topBar);
    topBarRight->setGeometry(1280 - 16 - TopBarRightWidget::preferredWidth(), 17,
                             TopBarRightWidget::preferredWidth(), 48);

    auto *backBtn = new QPushButton(page);
    backBtn->setGeometry(60, 103, 60, 60);
    backBtn->setCursor(Qt::PointingHandCursor);
    backBtn->setStyleSheet(
        "QPushButton{border:none;background:url(:/images/butt_back_up.png) no-repeat;}"
        "QPushButton:hover{background:url(:/images/butt_back_down.png) no-repeat;}"
    );
    connect(backBtn, &QPushButton::clicked, this, [this]() { openPage(3); });

    auto *inputWrap = new QWidget(page);
    inputWrap->setGeometry(335, 182, 610, 72);
    inputWrap->setStyleSheet("QWidget{border:1px solid #0068FF;background:rgba(255,255,255,0.1);}");

    m_jumpInput = new QLineEdit(inputWrap);
    m_jumpInput->setGeometry(0, 0, 610, 72);
    m_jumpInput->setStyleSheet("QLineEdit{border:none;background:transparent;color:#fff;font-size:48px;padding:0 24px;}");
    m_jumpInput->setValidator(new QIntValidator(1, m_pdfTotal, m_jumpInput));
    m_jumpInput->setText(QString::number(m_pdfPage));

    auto *keyPadWrap = new QWidget(page);
    keyPadWrap->setGeometry(335, 274, 610, 266);
    auto *grid = new QGridLayout(keyPadWrap);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(8);

    const QStringList keys = {
        QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("3"),
        QStringLiteral("4"), QStringLiteral("5"), QStringLiteral("6"),
        QStringLiteral("7"), QStringLiteral("8"), QStringLiteral("9"),
        QStringLiteral("确认"), QStringLiteral("0"), QStringLiteral("删")
    };

    for (int i = 0; i < keys.size(); ++i) {
        auto *btn = new QPushButton(keys.at(i), keyPadWrap);
        btn->setFixedSize(198, 60);
        btn->setCursor(Qt::PointingHandCursor);
        if (keys.at(i) == QStringLiteral("确认")) {
            btn->setStyleSheet(
                "QPushButton{border:1px solid #0068FF;background:url(:/images/butt_pdf_page_enter_up.png) no-repeat center center;color:transparent;}"
                "QPushButton:hover{background:url(:/images/butt_pdf_page_enter_down.png) no-repeat center center;}"
            );
        } else {
            btn->setStyleSheet(
                "QPushButton{border:1px solid #0068FF;background:rgba(255,255,255,0.1);color:#fff;font-size:36px;}"
                "QPushButton:hover{border-color:#00FAFF;color:#00FAFF;}"
            );
        }

        connect(btn, &QPushButton::clicked, this, [this, btn]() {
            const QString key = btn->text();
            if (key == QStringLiteral("确认")) {
                onConfirmPdfJump();
                return;
            }
            if (!m_jumpInput) {
                return;
            }
            if (key == QStringLiteral("删")) {
                QString text = m_jumpInput->text();
                text.chop(1);
                m_jumpInput->setText(text);
            } else {
                appendCharToInput(m_jumpInput, key);
            }
        });

        grid->addWidget(btn, i / 3, i % 3);
    }

    return page;
}

QWidget *DiagnosticWindow::createFaultDetailPage()
{
    auto *page = new QWidget();
    page->setStyleSheet("QWidget{background:transparent;}");

    auto *topBar = new QWidget(page);
    topBar->setGeometry(0, 0, 1280, 82);
    topBar->setStyleSheet("background: url(:/images/topbar.png) no-repeat;");

    auto *homeBtn = new QPushButton(topBar);
    homeBtn->setGeometry(12, 12, 48, 48);
    homeBtn->setCursor(Qt::PointingHandCursor);
    homeBtn->setStyleSheet(
        "QPushButton{border:none;background-image:url(:/images/pict_home_up.png);background-repeat:no-repeat;}"
        "QPushButton:hover{background-image:url(:/images/pict_home_down.png);}"
    );
    connect(homeBtn, &QPushButton::clicked, this, [this]() {
        emit requestReturnToMain();
        hide();
    });

    auto *title = new QLabel(QStringLiteral("故障诊断"), topBar);
    title->setGeometry(0, 0, 1280, 72);
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet("QLabel{color:#fff;font-size:36px;font-weight:700;background:transparent;}");
    title->setAttribute(Qt::WA_TransparentForMouseEvents);
    homeBtn->raise();

    auto *topBarRight = new TopBarRightWidget(topBar);
    topBarRight->setGeometry(1280 - 16 - TopBarRightWidget::preferredWidth(), 17,
                             TopBarRightWidget::preferredWidth(), 48);

    auto *backBtn = new QPushButton(page);
    backBtn->setGeometry(60, 103, 60, 60);
    backBtn->setCursor(Qt::PointingHandCursor);
    backBtn->setStyleSheet(
        "QPushButton{border:none;background:url(:/images/butt_back_up.png) no-repeat;}"
        "QPushButton:hover{background:url(:/images/butt_back_down.png) no-repeat;}"
    );
    connect(backBtn, &QPushButton::clicked, this, [this]() { openPage(1); });

    // 控制器标题（动态更新）
    m_faultDetailTitleLabel = new QLabel(page);
    m_faultDetailTitleLabel->setGeometry(147, 106, 986, 40);
    m_faultDetailTitleLabel->setStyleSheet(
        "QLabel{color:#eaf2ff;font-size:22px;background:transparent;}");

    // 固定表头行（SPN | FMI | 故障描述 | DTC码）
    // 列宽：95+88+395+396 = 974，加 3×4px 间距 = 986
    const int colW[] = {95, 88, 395, 396};
    const QStringList headers = {
        QStringLiteral("SPN"),
        QStringLiteral("FMI"),
        QStringLiteral("故障描述"),
        QStringLiteral("DTC码")
    };
    auto *headerRow = new QWidget(page);
    headerRow->setGeometry(147, 150, 986, 56);
    auto *headerLayout = new QHBoxLayout(headerRow);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(4);
    for (int c = 0; c < 4; ++c) {
        auto *cell = new QLabel(headers[c], headerRow);
        cell->setFixedSize(colW[c], 56);
        cell->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        cell->setStyleSheet(
            "QLabel{border:1px solid #0068FF;background:rgba(0,104,255,0.3);"
            "color:#fff;font-size:20px;padding-left:12px;font-weight:bold;}");
        headerLayout->addWidget(cell);
    }

    // 可滚动数据区域（内容由 populateFaultDetailContent() 动态填充）
    m_faultDetailScrollArea = new QScrollArea(page);
    m_faultDetailScrollArea->setGeometry(147, 210, 986, 468);
    m_faultDetailScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_faultDetailScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_faultDetailScrollArea->setWidgetResizable(true);
    m_faultDetailScrollArea->setStyleSheet(
        "QScrollArea{border:none;background:transparent;}"
        "QScrollBar:vertical{background:rgba(255,255,255,0.08);width:8px;border-radius:4px;margin:0;}"
        "QScrollBar::handle:vertical{background:rgba(0,104,255,0.7);border-radius:4px;min-height:20px;}"
        "QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;border:none;}"
    );
    m_faultDetailScrollArea->viewport()->setStyleSheet("background:transparent;");

    return page;
}

void DiagnosticWindow::resetPdfSearchState()
{
    if (m_searchInput) {
        m_searchInput->setText(QString());
    }
    m_pdfSearchKeyword.clear();
    m_pdfSearchMatches.clear();
    m_resultTotal = 0;
    m_resultIndex = 0;
    if (m_pdfSearchResultLabel) {
        m_pdfSearchResultLabel->setText(QStringLiteral("0/0"));
    }
    if (m_pdfSearchKeywordLabel) {
        m_pdfSearchKeywordLabel->setText(QStringLiteral("维修"));
    }
    if (m_pdfBottomNormal && m_pdfBottomSearch) {
        m_pdfBottomNormal->show();
        m_pdfBottomSearch->hide();
    }
}

void DiagnosticWindow::onOpenFaultDetailPage()
{
    showFaultDetail(QStringLiteral("ABS"));
}

void DiagnosticWindow::onOpenFaultPage()
{
    openPage(1);
}

void DiagnosticWindow::onOpenMaintenanceBookPage()
{
    openPage(2);
}

void DiagnosticWindow::onOpenPdfView()
{
    resetPdfSearchState();

    if (!m_currentPdfFilePath.isEmpty() && m_pdfDocument) {
        if (m_pdfDocument->openFile(m_currentPdfFilePath)) {
            m_pdfPage = 1;
            m_pdfTotal = qMax(1, m_pdfDocument->pageCount());
            updatePdfHeader();
            updatePdfView();
        }
    }

    openPage(3);
}

void DiagnosticWindow::onOpenPdfSearchPage()
{
    openPage(4);
}

void DiagnosticWindow::onOpenPdfJumpPage()
{
    if (m_jumpInput) {
        m_jumpInput->setText(QString::number(m_pdfPage));
    }
    openPage(5);
}

void DiagnosticWindow::onConfirmPdfSearch()
{
    if (!m_searchInput || !m_pdfDocument || !m_pdfDocument->isOpen()) {
        return;
    }

    const QString keyword = m_searchInput->text().trimmed();
    if (keyword.isEmpty()) {
        return;
    }

    m_pdfSearchKeyword = keyword;
    m_pdfSearchMatches.clear();
    const auto hits = m_pdfDocument->searchDocument(keyword);
    m_pdfSearchMatches.reserve(hits.size());
    for (const auto &hit : hits) {
        m_pdfSearchMatches.append({ hit.pageIndex, hit.bbox });
    }
    m_resultTotal = m_pdfSearchMatches.size();
    m_resultIndex = (m_resultTotal > 0 ? 1 : 0);

    if (m_pdfSearchKeywordLabel) {
        m_pdfSearchKeywordLabel->setText(keyword);
    }

    if (m_resultIndex > 0) {
        const int resultPage = m_pdfSearchMatches.at(0).pageIndex;
        m_pdfPage = qBound(1, resultPage + 1, m_pdfTotal);
    }

    updatePdfHeader();
    updateSearchResultHeader();

    if (m_pdfBottomNormal && m_pdfBottomSearch) {
        m_pdfBottomNormal->hide();
        m_pdfBottomSearch->show();
    }

    updatePdfView();
    openPage(3);
}

void DiagnosticWindow::onPrevSearchResult()
{
    if (m_resultIndex > 1) {
        --m_resultIndex;
        const int resultPage = m_pdfSearchMatches.at(m_resultIndex - 1).pageIndex;
        m_pdfPage = qBound(1, resultPage + 1, m_pdfTotal);
        updatePdfHeader();
        updateSearchResultHeader();
        updatePdfView();
    }
}

void DiagnosticWindow::onNextSearchResult()
{
    if (m_resultIndex < m_resultTotal) {
        ++m_resultIndex;
        const int resultPage = m_pdfSearchMatches.at(m_resultIndex - 1).pageIndex;
        m_pdfPage = qBound(1, resultPage + 1, m_pdfTotal);
        updatePdfHeader();
        updateSearchResultHeader();
        updatePdfView();
    }
}

void DiagnosticWindow::onConfirmPdfJump()
{
    if (!m_jumpInput) {
        return;
    }

    bool ok = false;
    const int page = m_jumpInput->text().toInt(&ok);
    if (!ok) {
        return;
    }

    m_pdfPage = qMax(1, qMin(page, m_pdfTotal));
    updatePdfHeader();
    updatePdfView();
    openPage(3);
}

void DiagnosticWindow::onPrevPage()
{
    if (m_pdfPage > 1) {
        --m_pdfPage;
        updatePdfHeader();
        updatePdfView();
    }
}

void DiagnosticWindow::onNextPage()
{
    if (m_pdfPage < m_pdfTotal) {
        ++m_pdfPage;
        updatePdfHeader();
        updatePdfView();
    }
}

void DiagnosticWindow::openPage(int index)
{
    if (!m_pages || index < 0 || index >= m_pages->count()) {
        return;
    }
    m_pages->setCurrentIndex(index);
}

void DiagnosticWindow::presentPage(int pageIndex, bool directFromMain)
{
    m_directEntryFromMain = directFromMain && pageIndex != 0;
    openPage(pageIndex);
}

void DiagnosticWindow::onBackFromTopLevelSubPage()
{
    if (m_directEntryFromMain) {
        m_directEntryFromMain = false;
        emit requestReturnToMain();
        hide();
        return;
    }
    openPage(0);
}

void DiagnosticWindow::appendCharToInput(QLineEdit *target, const QString &text)
{
    if (!target) {
        return;
    }
    target->setText(target->text() + text);
}

void DiagnosticWindow::updatePdfHeader()
{
    if (!m_pdfHeaderLabel) {
        return;
    }
    m_pdfHeaderLabel->setText(QStringLiteral("第%1/%2页").arg(m_pdfPage).arg(m_pdfTotal));
}

void DiagnosticWindow::updatePdfView()
{
    if (!m_pdfDocument || !m_pdfRenderLabel || !m_pdfDocument->isOpen()) {
        return;
    }

    const int width = m_pdfRenderLabel->width();
    const int height = m_pdfRenderLabel->height();
    QImage pageImage = m_pdfDocument->renderPage(m_pdfPage - 1, width, height);
    if (pageImage.isNull()) {
        return;
    }

    const QSize labelSize = m_pdfRenderLabel->size();
    const QImage scaledImage = pageImage.scaled(labelSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    const qreal scaleX = qreal(scaledImage.width()) / qreal(pageImage.width());
    const qreal scaleY = qreal(scaledImage.height()) / qreal(pageImage.height());
    const QPoint offset((labelSize.width() - scaledImage.width()) / 2,
                        (labelSize.height() - scaledImage.height()) / 2);

    QPixmap canvas(labelSize);
    canvas.fill(Qt::transparent);
    QPainter painter(&canvas);
    painter.drawImage(offset, scaledImage);
    painter.setRenderHint(QPainter::Antialiasing, true);

    if (!m_pdfSearchMatches.isEmpty()) {
        const int displayedPageIndex = m_pdfPage - 1;
        const int selectedIndex = (m_resultIndex > 0 ? m_resultIndex - 1 : -1);
        const QRectF pageBounds = m_pdfDocument->pageBounds(displayedPageIndex);
        const qreal pageOriginX = pageBounds.left();
        const qreal pageOriginY = pageBounds.top();
        const qreal pageHeight = pageBounds.height();

        for (int i = 0; i < m_pdfSearchMatches.size(); ++i) {
            const auto &hit = m_pdfSearchMatches.at(i);
            if (hit.pageIndex != displayedPageIndex) {
                continue;
            }

            const QRectF pdfRect = hit.rect.normalized();
            QRectF imageRect = m_pdfDocument->mapPdfRectToImage(displayedPageIndex, pdfRect, pageImage.width(), pageImage.height());
            if (imageRect.isNull() || imageRect.isEmpty()) {
                const qreal scaleImageX = pageImage.width() / pageBounds.width();
                const qreal scaleImageY = pageImage.height() / pageBounds.height();
                const qreal left = (pdfRect.left() - pageOriginX) * scaleImageX;
                const qreal right = (pdfRect.right() - pageOriginX) * scaleImageX;
                const qreal top = (pdfRect.top() - pageOriginY) * scaleImageY;
                const qreal bottom = (pdfRect.bottom() - pageOriginY) * scaleImageY;
                imageRect = QRectF(QPointF(left, top), QPointF(right, bottom)).normalized();
            }
            QRectF displayRect(
                QPointF(imageRect.left() * scaleX, imageRect.top() * scaleY),
                QSizeF(imageRect.width() * scaleX, imageRect.height() * scaleY)
            );
            displayRect.translate(offset);

            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(255, 255, 0, 140));
            painter.drawRect(displayRect);

            if (i == selectedIndex) {
                painter.setBrush(Qt::NoBrush);
                painter.setPen(QPen(QColor(255, 50, 50), 4));
                painter.drawRect(displayRect);
            } else {
                painter.setBrush(Qt::NoBrush);
                painter.setPen(QPen(QColor(255, 165, 0), 2));
                painter.drawRect(displayRect);
            }
        }
    }

    painter.end();
    m_pdfRenderLabel->setPixmap(canvas);
}

void DiagnosticWindow::updateSearchResultHeader()
{
    if (!m_pdfSearchResultLabel) {
        return;
    }
    const int displayIndex = (m_resultTotal > 0 ? qMax(1, m_resultIndex) : 0);
    m_pdfSearchResultLabel->setText(QStringLiteral("%1/%2").arg(displayIndex).arg(m_resultTotal));
}

void DiagnosticWindow::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_VolumeUp:
        AppSignals::changeVolume(+1);
        break;
    case Qt::Key_VolumeDown:
        AppSignals::changeVolume(-1);
        break;
    case Qt::Key_HomePage:
        emit requestReturnToMain();
        hide();
        break;
    case Qt::Key_Back:
    case Qt::Key_Escape:
        // 按当前页分层回退，而非直接跳到首页：
        //   页面索引：0=主菜单, 1=故障查询, 2=维护资料列表, 3=PDF阅读,
        //             4=PDF搜索, 5=PDF跳页, 6=故障详情
        if (!m_pages) break;
        switch (m_pages->currentIndex()) {
        case 0:  // 主菜单 → 返回应用主界面
            emit requestReturnToMain();
            hide();
            break;
        case 1:  // 故障列表
        case 2:  // 维护资料列表
            onBackFromTopLevelSubPage();
            break;
        case 3:  // PDF阅读 → 维护资料列表
            openPage(2);
            break;
        case 4:  // PDF搜索 → PDF阅读
        case 5:  // PDF跳页 → PDF阅读
            openPage(3);
            break;
        case 6:  // 故障详情 → 故障列表
            openPage(1);
            break;
        default:
            openPage(0);
            break;
        }
        break;
    default:
        QMainWindow::keyPressEvent(event);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  故障诊断实时数据相关实现
// ─────────────────────────────────────────────────────────────────────────────

int DiagnosticWindow::controllerIndex(const QString &ctrl)
{
    if (ctrl == QLatin1String("ABS")) return 0;
    if (ctrl == QLatin1String("EBS")) return 1;
    if (ctrl == QLatin1String("BCM")) return 2;
    return -1;
}

void DiagnosticWindow::onFaultDataReceived(const QString &controller,
                                            const QVector<McuFaultInfo> &faults)
{
    m_activeFaults[controller] = faults;

    // 更新对应系统按钮上的徽标数量
    const int idx = controllerIndex(controller);
    if (idx >= 0 && m_faultBadgeLabels[idx]) {
        const int cnt = faults.size();
        if (cnt > 0) {
            m_faultBadgeLabels[idx]->setText(QString::number(cnt));
            m_faultBadgeLabels[idx]->show();
        } else {
            m_faultBadgeLabels[idx]->hide();
        }
    }

    // 若详情页正在显示该控制器的故障，立即刷新
    if (m_pages && m_pages->currentIndex() == 6 &&
        m_currentFaultController == controller) {
        populateFaultDetailContent();
    }
}

void DiagnosticWindow::showFaultDetail(const QString &controller)
{
    m_currentFaultController = controller;
    populateFaultDetailContent();
    openPage(6);
}

void DiagnosticWindow::populateFaultDetailContent()
{
    if (!m_faultDetailScrollArea || !m_faultDetailTitleLabel)
        return;

    const QString ctrl = m_currentFaultController;
    const QVector<McuFaultInfo> &faults = m_activeFaults.value(ctrl);

    // 控制器显示名称映射
    static const QStringList kCtrlKeys  = {
        QStringLiteral("ABS"), QStringLiteral("EBS"), QStringLiteral("BCM")};
    static const QStringList kCtrlNames = {
        QStringLiteral("ABS系统"),
        QStringLiteral("双预警系统"),
        QStringLiteral("车身控制器")};
    const int ci = kCtrlKeys.indexOf(ctrl);
    const QString dispName = (ci >= 0) ? kCtrlNames.at(ci) : ctrl;

    m_faultDetailTitleLabel->setText(
        faults.isEmpty()
            ? QStringLiteral("%1  —  当前无故障").arg(dispName)
            : QStringLiteral("%1  —  %2 个故障").arg(dispName).arg(faults.size()));

    // 清除旧内容
    if (QWidget *old = m_faultDetailScrollArea->takeWidget())
        old->deleteLater();

    // 生成新内容
    // 列宽：95(SPN)+88(FMI)+395(故障描述)+396(DTC码) + 3×4gap = 986
    const int colW[] = {95, 88, 395, 396};

    auto *content = new QWidget();
    content->setStyleSheet("QWidget{background:transparent;}");
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    if (faults.isEmpty()) {
        auto *empty = new QLabel(QStringLiteral("当前控制器无故障"), content);
        empty->setAlignment(Qt::AlignCenter);
        empty->setStyleSheet(
            "QLabel{color:#88aacc;font-size:26px;background:transparent;padding:60px 0;}");
        layout->addWidget(empty);
    } else {
        for (const McuFaultInfo &f : faults) {
            const QString desc = FaultCodeDb::lookup(ctrl, f.spn, f.fmi);
            const QString dtc  = FaultCodeDb::dtcCode(ctrl, f.spn, f.fmi);

            auto *row = new QWidget(content);
            row->setFixedHeight(56);
            auto *rl = new QHBoxLayout(row);
            rl->setContentsMargins(0, 0, 0, 0);
            rl->setSpacing(4);

            const QStringList texts = {
                QString::number(f.spn),
                QString::number(f.fmi),
                desc.isEmpty() ? f.rawDesc : desc,
                dtc.isEmpty()  ? QStringLiteral("—") : dtc
            };
            for (int c = 0; c < 4; ++c) {
                auto *cell = new QLabel(texts[c], row);
                cell->setFixedSize(colW[c], 56);
                cell->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
                cell->setWordWrap(c == 2);  // 故障描述列允许换行
                cell->setStyleSheet(
                    "QLabel{border:1px solid rgba(0,104,255,0.5);"
                    "background:rgba(255,255,255,0.07);color:#eaf2ff;"
                    "font-size:18px;padding-left:8px;padding-right:4px;}");
                rl->addWidget(cell);
            }
            layout->addWidget(row);
        }
    }

    layout->addStretch();
    m_faultDetailScrollArea->setWidget(content);
}
