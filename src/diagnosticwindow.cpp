#include "diagnosticwindow.h"
#include "devicedetect.h"
#include "topbarwidget.h"

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
#include <QStackedWidget>
#include <QTime>
#include <QVBoxLayout>

DiagnosticWindow::DiagnosticWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_pages(nullptr)
    , m_pdfHeaderLabel(nullptr)
    , m_pdfSearchKeywordLabel(nullptr)
    , m_pdfSearchResultLabel(nullptr)
    , m_searchInput(nullptr)
    , m_jumpInput(nullptr)
    , m_pdfBottomNormal(nullptr)
    , m_pdfBottomSearch(nullptr)
    , m_pdfPage(1)
    , m_pdfTotal(10)
    , m_resultIndex(1)
    , m_resultTotal(8)
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
}

void DiagnosticWindow::closeEvent(QCloseEvent *event)
{
    emit requestReturnToMain();
    QMainWindow::closeEvent(event);
}

void DiagnosticWindow::setupUI()
{
    auto *central = new QWidget(this);
    setCentralWidget(central);
    central->setStyleSheet("QWidget{background: url(:/images/inside_background.png) no-repeat center center;color:#eaf2ff;}");

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
    connect(backBtn, &QPushButton::clicked, this, [this]() { openPage(0); });

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
        if (i == 0) {
            // ABS 系统：有故障徽标，点击进入故障详情页
            connect(btn, &QPushButton::clicked, this, &DiagnosticWindow::onOpenFaultDetailPage);
            auto *tips = new QLabel(QStringLiteral("1"), btn);
            tips->setGeometry(77, -8, 32, 32);
            tips->setAlignment(Qt::AlignCenter);
            tips->setStyleSheet("QLabel{background:#B82F2F;color:#fff;font-size:24px;border-radius:16px;}");
        }
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
    connect(backBtn, &QPushButton::clicked, this, [this]() { openPage(0); });

    auto *bookWrap = new QWidget(page);
    // CSS .diagnostic_maintenance_book { width:944; margin:24px auto }
    // => x=(1280-944)/2=168, y=topbar(82)+24=106
    // 8 rows × 68px + 7 gaps × 2px = 558px
    bookWrap->setGeometry(168, 106, 944, 558);
    auto *bookLayout = new QVBoxLayout(bookWrap);
    bookLayout->setContentsMargins(0, 0, 0, 0);
    bookLayout->setSpacing(2);

    const QStringList books = {
        QStringLiteral("整车|第1册 共13册"),
        QStringLiteral("发动机|第2册 共13册"),
        QStringLiteral("发动机附件|第3册 共13册"),
        QStringLiteral("电控|第4册 共13册"),
        QStringLiteral("电器|第5册 共13册"),
        QStringLiteral("整车|第6册 共13册"),
        QStringLiteral("整车|第7册 共13册"),
        QStringLiteral("整车|第8册 共13册")
    };

    for (const QString &row : books) {
        const QStringList parts = row.split('|');
        auto *line = new QPushButton(bookWrap);
        line->setCursor(Qt::PointingHandCursor);
        line->setFixedHeight(68);
        line->setStyleSheet(
            "QPushButton{border:none;background:rgba(255,255,255,0.1);text-align:left;padding:0 24px;}"
            "QPushButton:hover{background:rgba(0,104,255,0.35);}"
        );
        auto *lineLayout = new QHBoxLayout(line);
        lineLayout->setContentsMargins(24, 0, 24, 0);

        auto *left = new QLabel(parts.value(0), line);
        left->setStyleSheet("QLabel{color:#fff;font-size:24px;background:transparent;}");
        auto *right = new QLabel(parts.value(1), line);
        right->setStyleSheet("QLabel{color:#fff;font-size:24px;background:transparent;}");
        lineLayout->addWidget(left);
        lineLayout->addStretch();
        lineLayout->addWidget(right);

        connect(line, &QPushButton::clicked, this, &DiagnosticWindow::onOpenPdfView);
        bookLayout->addWidget(line);
    }

    return page;
}

QWidget *DiagnosticWindow::createPdfPage()
{
    auto *page = new QWidget();
    page->setStyleSheet("QWidget{background:#525659;}");

    auto *paper = new QLabel(page);
    paper->setGeometry(454, 110, 372, 500);
    paper->setStyleSheet("QLabel{background:rgba(255,255,255,0.1);}");

    auto *paperImage = new QLabel(paper);
    paperImage->setGeometry(0, 0, 372, 500);
    paperImage->setPixmap(QPixmap(":/images/pdf_view.png").scaled(372, 500, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));

    auto *topOverlay = new QWidget(page);
    topOverlay->setGeometry(0, 0, 1280, 72);
    topOverlay->setStyleSheet("background:rgba(0,0,0,0.5);");

    auto *homeBtn = new QPushButton(topOverlay);
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

    auto *backBtn = new QPushButton(topOverlay);
    backBtn->setGeometry(60, 12, 48, 48);
    backBtn->setCursor(Qt::PointingHandCursor);
    backBtn->setStyleSheet(
        "QPushButton{border:none;background-image:url(:/images/butt_video_back_up.png);background-repeat:no-repeat;background-position:center;}"
        "QPushButton:hover{background-image:url(:/images/butt_video_back_down.png);}"
    );
    connect(backBtn, &QPushButton::clicked, this, [this]() { openPage(2); });

    auto *title = new QLabel(QStringLiteral("使用维护"), topOverlay);
    title->setGeometry(100, 0, 980, 72);
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet("QLabel{color:#fff;font-size:36px;background:transparent;}");
    title->setAttribute(Qt::WA_TransparentForMouseEvents);
    homeBtn->raise();

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
        "QPushButton{border:none;background-image:url(:/images/butt_music_prev_up.png);}"
        "QPushButton:hover{background-image:url(:/images/butt_music_prev_down.png);}"
    );
    connect(prevBtn, &QPushButton::clicked, this, &DiagnosticWindow::onPrevPage);

    auto *hiddenCenter = new QWidget(m_pdfBottomNormal);
    hiddenCenter->setFixedSize(84, 84);

    auto *nextBtn = new QPushButton(m_pdfBottomNormal);
    nextBtn->setFixedSize(60, 60);
    nextBtn->setCursor(Qt::PointingHandCursor);
    nextBtn->setStyleSheet(
        "QPushButton{border:none;background-image:url(:/images/butt_music_next_up.png);}"
        "QPushButton:hover{background-image:url(:/images/butt_music_next_down.png);}"
    );
    connect(nextBtn, &QPushButton::clicked, this, &DiagnosticWindow::onNextPage);

    auto *searchBtn = new QPushButton(m_pdfBottomNormal);
    searchBtn->setFixedSize(60, 60);
    searchBtn->setCursor(Qt::PointingHandCursor);
    searchBtn->setStyleSheet(
        "QPushButton{border:none;background-image:url(:/images/butt_radio_search_up.png);}"
        "QPushButton:hover{background-image:url(:/images/butt_radio_search_down.png);}"
    );
    connect(searchBtn, &QPushButton::clicked, this, &DiagnosticWindow::onOpenPdfSearchPage);

    btnLayout->addWidget(prevBtn);
    btnLayout->addWidget(hiddenCenter);
    btnLayout->addWidget(nextBtn);
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
        "QPushButton{border:none;background-image:url(:/images/butt_radio_search_del_all_up.png);}"
        "QPushButton:hover{background-image:url(:/images/butt_radio_search_del_all_down.png);}"
    );
    connect(closeResultBtn, &QPushButton::clicked, this, [this]() {
        if (m_pdfBottomNormal && m_pdfBottomSearch) {
            m_pdfBottomNormal->show();
            m_pdfBottomSearch->hide();
        }
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
        "QPushButton{border:none;background:url(:/images/butt_pdf_serch_result_pre_up.png) no-repeat center center;}"
        "QPushButton:hover{background:url(:/images/butt_pdf_serch_result_pre_down.png) no-repeat center center;}"
    );
    connect(prevResult, &QPushButton::clicked, this, &DiagnosticWindow::onPrevSearchResult);

    m_pdfSearchResultLabel = new QLabel(resultWrap);
    m_pdfSearchResultLabel->setStyleSheet("QLabel{color:#fff;font-size:48px;background:transparent;}");

    auto *nextResult = new QPushButton(resultWrap);
    nextResult->setFixedSize(48, 72);
    nextResult->setCursor(Qt::PointingHandCursor);
    nextResult->setStyleSheet(
        "QPushButton{border:none;background:url(:/images/butt_pdf_serch_result_next_up.png) no-repeat center center;}"
        "QPushButton:hover{background:url(:/images/butt_pdf_serch_result_next_down.png) no-repeat center center;}"
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
    inputWrap->setGeometry(232, 182, 816, 72);
    inputWrap->setStyleSheet("QWidget{border:1px solid #0068FF;background:rgba(255,255,255,0.1);}");

    m_searchInput = new QLineEdit(inputWrap);
    m_searchInput->setGeometry(0, 0, 816, 72);
    m_searchInput->setStyleSheet("QLineEdit{border:none;background:transparent;color:#fff;font-size:48px;padding:0 24px;}");
    m_searchInput->setText(QStringLiteral("维修"));

    auto *selectWrap = new QWidget(page);
    selectWrap->setGeometry(232, 262, 816, 72);
    selectWrap->setStyleSheet("QWidget{border:1px solid #0068FF;background:transparent;}");

    const QStringList candidates = {QStringLiteral("一"), QStringLiteral("厂"), QStringLiteral("大"), QStringLiteral("木"), QStringLiteral("林")};
    for (int i = 0; i < candidates.size(); ++i) {
        auto *btn = new QPushButton(candidates.at(i), selectWrap);
        btn->setGeometry(24 + i * 68, 1, 68, 68);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet(
            i == 0
                ? "QPushButton{border:none;background:#00FAFF;color:#fff;font-size:48px;}"
                : "QPushButton{border:none;background:transparent;color:#fff;font-size:48px;}QPushButton:hover{color:#00FAFF;}"
        );
        connect(btn, &QPushButton::clicked, this, [this, btn]() {
            appendCharToInput(m_searchInput, btn->text());
        });
    }

    auto *writeArea = new QLabel(page);
    writeArea->setGeometry(335, 344, 610, 240);
    writeArea->setStyleSheet("QLabel{background:url(:/images/pict_pdf_serch_writearea.png) no-repeat center center;}");

    auto *keyPadWrap = new QWidget(page);
    keyPadWrap->setGeometry(335, 360, 610, 210);
    auto *grid = new QGridLayout(keyPadWrap);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(8);

    const QStringList keys = {
        QStringLiteral("一"), QStringLiteral("二"), QStringLiteral("三"),
        QStringLiteral("四"), QStringLiteral("五"), QStringLiteral("六"),
        QStringLiteral("七"), QStringLiteral("八"), QStringLiteral("九"),
        QStringLiteral("删"), QStringLiteral("〇"), QStringLiteral("空")
    };

    for (int i = 0; i < keys.size(); ++i) {
        auto *btn = new QPushButton(keys.at(i), keyPadWrap);
        btn->setFixedSize(198, 46);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet(
            "QPushButton{border:1px solid #0068FF;background:rgba(255,255,255,0.1);color:#fff;font-size:28px;}"
            "QPushButton:hover{border-color:#00FAFF;color:#00FAFF;}"
        );
        connect(btn, &QPushButton::clicked, this, [this, btn]() {
            const QString key = btn->text();
            if (key == QStringLiteral("删")) {
                if (m_searchInput) {
                    QString text = m_searchInput->text();
                    text.chop(1);
                    m_searchInput->setText(text);
                }
            } else if (key == QStringLiteral("空")) {
                appendCharToInput(m_searchInput, QStringLiteral(" "));
            } else {
                appendCharToInput(m_searchInput, key);
            }
        });
        grid->addWidget(btn, i / 3, i % 3);
    }

    auto *confirmBtn = new QPushButton(QStringLiteral("确认"), page);
    confirmBtn->setGeometry(829, 592, 116, 54);
    confirmBtn->setCursor(Qt::PointingHandCursor);
    confirmBtn->setStyleSheet(
        "QPushButton{border:2px solid #0068FF;background:transparent;color:#fff;font-size:28px;}"
        "QPushButton:hover{border-color:#00FAFF;color:#00FAFF;}"
    );
    connect(confirmBtn, &QPushButton::clicked, this, &DiagnosticWindow::onConfirmPdfSearch);

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

    auto *title = new QLabel(QStringLiteral("诊断维护"), topBar);
    title->setGeometry(0, 0, 1280, 72);
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet("QLabel{color:#fff;font-size:36px;font-weight:700;background:transparent;}");
    title->setAttribute(Qt::WA_TransparentForMouseEvents);
    homeBtn->raise();

    auto *topBarRight = new TopBarRightWidget(topBar);
    topBarRight->setGeometry(1280 - 16 - TopBarRightWidget::preferredWidth(), 17,
                             TopBarRightWidget::preferredWidth(), 48);

    // .back { left:60; top:103; w:60; h:60 }  → 返回故障列表
    auto *backBtn = new QPushButton(page);
    backBtn->setGeometry(60, 103, 60, 60);
    backBtn->setCursor(Qt::PointingHandCursor);
    backBtn->setStyleSheet(
        "QPushButton{border:none;background:url(:/images/butt_back_up.png) no-repeat;}"
        "QPushButton:hover{background:url(:/images/butt_back_down.png) no-repeat;}"
    );
    connect(backBtn, &QPushButton::clicked, this, [this]() { openPage(1); });

    // CSS .diagnostic_fault_detail { width:986; margin:24px auto }
    // => x=(1280-986)/2=147, y=82+24=106
    // 列宽(含border): 95 | 88 | 395 | 396，gap=4px → 95+88+395+396+3×4 = 986 ✓
    // 1行表头 + 8行数据，行高60px，行间距4px => 9×60+8×4=572px
    auto *detailWrap = new QWidget(page);
    detailWrap->setGeometry(147, 106, 986, 572);
    detailWrap->setStyleSheet("QWidget{background:transparent;}");
    auto *detailLayout = new QVBoxLayout(detailWrap);
    detailLayout->setContentsMargins(0, 0, 0, 0);
    detailLayout->setSpacing(4);

    struct RowData { QString c1, c2, c3, c4; };
    const QList<RowData> rows = {
        {"SPN",  "FMI", "车身控制器",      "维修建议"},
        {"790",  "8",   "轴右轮速传感器",   "维修建议内容"},
        {"790",  "8",   "轴右轮速传感器",   "维修建议内容"},
        {"790",  "8",   "轴右轮速传感器",   "维修建议内容"},
        {"790",  "8",   "轴右轮速传感器",   "维修建议内容"},
        {"790",  "8",   "轴右轮速传感器",   "维修建议内容"},
        {"790",  "8",   "轴右轮速传感器",   "维修建议内容"},
        {"790",  "8",   "轴右轮速传感器",   "维修建议内容"},
        {"790",  "8",   "轴右轮速传感器",   "维修建议内容"},
    };
    const int colWidths[] = {95, 88, 395, 396};

    for (int r = 0; r < rows.size(); ++r) {
        auto *rowWidget = new QWidget(detailWrap);
        rowWidget->setFixedHeight(60);
        auto *rowLayout = new QHBoxLayout(rowWidget);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(4);

        const QStringList texts = {rows[r].c1, rows[r].c2, rows[r].c3, rows[r].c4};
        const QString baseStyle = r == 0
            ? "QLabel{border:1px solid #0068FF;background:rgba(255,255,255,0.1);color:#fff;font-size:20px;padding-left:24px;font-weight:bold;line-height:60px;}"
            : "QLabel{border:1px solid #0068FF;background:rgba(255,255,255,0.1);color:#fff;font-size:20px;padding-left:24px;line-height:60px;}";

        for (int c = 0; c < 4; ++c) {
            auto *cell = new QLabel(texts[c], rowWidget);
            cell->setFixedSize(colWidths[c], 60);
            cell->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
            cell->setStyleSheet(baseStyle);
            rowLayout->addWidget(cell);
        }
        detailLayout->addWidget(rowWidget);
    }

    return page;
}

void DiagnosticWindow::onOpenFaultDetailPage()
{
    openPage(6);
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
    if (m_pdfBottomNormal && m_pdfBottomSearch) {
        m_pdfBottomNormal->show();
        m_pdfBottomSearch->hide();
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
    if (!m_searchInput) {
        return;
    }

    const QString keyword = m_searchInput->text().trimmed();
    if (keyword.isEmpty()) {
        return;
    }

    m_resultIndex = 1;
    m_resultTotal = 8;

    if (m_pdfSearchKeywordLabel) {
        m_pdfSearchKeywordLabel->setText(keyword);
    }
    updateSearchResultHeader();

    if (m_pdfBottomNormal && m_pdfBottomSearch) {
        m_pdfBottomNormal->hide();
        m_pdfBottomSearch->show();
    }
    openPage(3);
}

void DiagnosticWindow::onPrevSearchResult()
{
    if (m_resultIndex > 1) {
        --m_resultIndex;
        updateSearchResultHeader();
    }
}

void DiagnosticWindow::onNextSearchResult()
{
    if (m_resultIndex < m_resultTotal) {
        ++m_resultIndex;
        updateSearchResultHeader();
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
    openPage(3);
}

void DiagnosticWindow::onPrevPage()
{
    if (m_pdfPage > 1) {
        --m_pdfPage;
        updatePdfHeader();
    }
}

void DiagnosticWindow::onNextPage()
{
    if (m_pdfPage < m_pdfTotal) {
        ++m_pdfPage;
        updatePdfHeader();
    }
}

void DiagnosticWindow::openPage(int index)
{
    if (!m_pages || index < 0 || index >= m_pages->count()) {
        return;
    }
    m_pages->setCurrentIndex(index);
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

void DiagnosticWindow::updateSearchResultHeader()
{
    if (!m_pdfSearchResultLabel) {
        return;
    }
    m_pdfSearchResultLabel->setText(QStringLiteral("%1/%2").arg(m_resultIndex).arg(m_resultTotal));
}

void DiagnosticWindow::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_VolumeUp:
        QProcess::startDetached("amixer", {"sset", "LINEOUT volume", "5%+"});
        break;
    case Qt::Key_VolumeDown:
        QProcess::startDetached("amixer", {"sset", "LINEOUT volume", "5%-"});
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
        case 1:  // 故障列表 → 主菜单
        case 2:  // 维护资料列表 → 主菜单
            openPage(0);
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
