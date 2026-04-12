#include "phonewindow.h"
#include "topbarwidget.h"
#include "appsignals.h"

#include <QApplication>
#include <QKeyEvent>
#include <QProcess>
#include <QCloseEvent>
#include <QDateTime>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QScreen>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>

PhoneWindow::PhoneWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_tabStack(nullptr)
    , m_tabDial(nullptr)
    , m_tabHistory(nullptr)
    , m_tabContacts(nullptr)
    , m_numberEdit(nullptr)
    , m_callNumber(nullptr)
    , m_callTimer(nullptr)
    , m_callStateLabel(nullptr)
    , m_historyList(nullptr)
    , m_contactList(nullptr)
    , m_detailOverlay(nullptr)
    , m_detailNameLabel(nullptr)
    , m_detailNumberLabel(nullptr)
    , m_callOverlay(nullptr)
    , m_callKeyboardPanel(nullptr)
    , m_answerButton(nullptr) {
    setWindowTitle("蓝牙电话");
    setFixedSize(1280, 720);
    if (QApplication::primaryScreen()) {
        move(QApplication::primaryScreen()->geometry().center() - rect().center());
    }
    setupUI();
    activateTab(0);
}

void PhoneWindow::closeEvent(QCloseEvent *event) {
    emit requestReturnToMain();
    QMainWindow::closeEvent(event);
}

void PhoneWindow::setupUI() {
    QWidget *central = new QWidget(this);
    central->setStyleSheet("background-image:url(:/images/inside_background.png); background-repeat:no-repeat;");
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

    QLabel *title = new QLabel("蓝牙电话", this);
    title->setStyleSheet("color:#fff;font-size:36px;font-weight:bold;background:transparent;");
    top->addWidget(title, 0, 1, Qt::AlignCenter);

    auto *topBarRight = new TopBarRightWidget(topBar);
    top->addWidget(topBarRight, 0, 2, Qt::AlignRight | Qt::AlignVCenter);

    main->addWidget(topBar);

    QWidget *tabWrap = new QWidget(this);
    tabWrap->setFixedHeight(84);
    QHBoxLayout *tabs = new QHBoxLayout(tabWrap);
    tabs->setContentsMargins(396, 18, 396, 0);
    tabs->setSpacing(1);

    auto mkTab = [this](const QString &text) {
        QPushButton *b = new QPushButton(text, this);
        b->setFixedSize(160, 66);
        b->setCursor(Qt::PointingHandCursor);
        b->setStyleSheet("QPushButton{border:none;color:#fff;font-size:28px;background-repeat:no-repeat;background-position:center;} QPushButton:hover{color:#00FAFF;}");
        return b;
    };

    m_tabDial = mkTab("拨号键盘");
    m_tabHistory = mkTab("通话记录");
    m_tabContacts = mkTab("通讯录");

    tabs->addWidget(m_tabDial);
    tabs->addWidget(m_tabHistory);
    tabs->addWidget(m_tabContacts);

    connect(m_tabDial, &QPushButton::clicked, this, &PhoneWindow::onDialTab);
    connect(m_tabHistory, &QPushButton::clicked, this, &PhoneWindow::onHistoryTab);
    connect(m_tabContacts, &QPushButton::clicked, this, &PhoneWindow::onContactsTab);

    main->addWidget(tabWrap);

    m_tabStack = new QStackedWidget(this);

    QWidget *dialPage = new QWidget(this);
    QVBoxLayout *dial = new QVBoxLayout(dialPage);
    dial->setContentsMargins(232, 24, 232, 20);
    dial->setSpacing(8);

    m_numberEdit = new QLineEdit("18800001234", this);
    m_numberEdit->setFixedSize(816, 72);
    m_numberEdit->setStyleSheet("QLineEdit{color:#fff;font-size:48px;background:rgba(255,255,255,0.1);border:1px solid #0068FF;padding:0 20px;}");
    dial->addWidget(m_numberEdit);

    QWidget *contentWrap = new QWidget(this);
    QHBoxLayout *contentLayout = new QHBoxLayout(contentWrap);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(8);

    QWidget *kb = new QWidget(this);
    kb->setFixedWidth(610);
    QGridLayout *grid = new QGridLayout(kb);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(8);

    const QStringList keys = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "*", "0", "#"};
    const QStringList upIcons = {
        ":/images/butt_calling_num1_up.png",
        ":/images/butt_calling_num2_up.png",
        ":/images/butt_calling_num3_up.png",
        ":/images/butt_calling_num4_up.png",
        ":/images/butt_calling_num5_up.png",
        ":/images/butt_calling_num6_up.png",
        ":/images/butt_calling_num7_up.png",
        ":/images/butt_calling_num8_up.png",
        ":/images/butt_calling_num9_up.png",
        ":/images/butt_calling_num10_up.png",
        ":/images/butt_calling_num0_up.png",
        ":/images/butt_calling_num12_up.png"
    };
    const QStringList downIcons = {
        ":/images/butt_calling_num1_down.png",
        ":/images/butt_calling_num2_down.png",
        ":/images/butt_calling_num3_down.png",
        ":/images/butt_calling_num4_down.png",
        ":/images/butt_calling_num5_down.png",
        ":/images/butt_calling_num6_down.png",
        ":/images/butt_calling_num7_down.png",
        ":/images/butt_calling_num8_down.png",
        ":/images/butt_calling_num9_down.png",
        ":/images/butt_calling_num10_down.png",
        ":/images/butt_calling_num0_down.png",
        ":/images/butt_calling_num12_down.png"
    };
    for (int i = 0; i < keys.size(); ++i) {
        QPushButton *key = new QPushButton(this);
        key->setProperty("digit", keys.at(i));
        key->setFixedSize(198, 120);
        key->setStyleSheet(
            QString("QPushButton{border:none;background:url(%1) no-repeat center center;}"
                    "QPushButton:hover{background-image:url(%2);}").arg(upIcons.at(i), downIcons.at(i))
        );
        connect(key, &QPushButton::clicked, this, [this, key]() { appendDigit(key->property("digit").toString()); });
        grid->addWidget(key, i / 3, i % 3);
    }

    contentLayout->addWidget(kb);

    QWidget *action = new QWidget(this);
    action->setFixedWidth(198);
    QHBoxLayout *actionLayout = new QHBoxLayout(action);
    actionLayout->setContentsMargins(0, 0, 0, 0);
    actionLayout->setSpacing(8);
    actionLayout->setDirection(QBoxLayout::TopToBottom);

    QPushButton *delBtn = new QPushButton(this);
    delBtn->setFixedSize(198, 196);
    delBtn->setStyleSheet("QPushButton{border:none;background:url(:/images/butt_calling_del_up.png) no-repeat center center;}"
                          "QPushButton:hover{background:url(:/images/butt_calling_del_down.png) no-repeat center center;}");
    connect(delBtn, &QPushButton::clicked, this, [this]() {
        m_numberEdit->setText(m_numberEdit->text().left(m_numberEdit->text().size() - 1));
    });

    QPushButton *dialBtn = new QPushButton("拨号", this);
    dialBtn->setFixedSize(198, 196);
    dialBtn->setStyleSheet("QPushButton{color:#fff;font-size:48px;font-weight:bold;border:none;background:#0068FF;text-align:center;}"
                           "QPushButton:hover{background:#00FAFF;}");
    connect(dialBtn, &QPushButton::clicked, this, &PhoneWindow::onDial);

    actionLayout->addWidget(delBtn);
    actionLayout->addWidget(dialBtn);

    contentLayout->addWidget(action);
    dial->addWidget(contentWrap, 0, Qt::AlignHCenter);

    QWidget *historyPage = new QWidget(this);
    QVBoxLayout *history = new QVBoxLayout(historyPage);
    history->setContentsMargins(160, 20, 160, 20);
    m_historyList = new QListWidget(this);
    m_historyList->setStyleSheet("QListWidget{background:transparent;border:none;outline:none;} QListWidget::item{border:none;} QListWidget::item:selected{background:transparent;}");
    m_historyList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_historyList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_historyList->setSpacing(2);
    populateHistoryList();
    connect(m_historyList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        const QString number = item->data(Qt::UserRole).toString();
        if (!number.isEmpty()) {
            m_numberEdit->setText(number);
            activateTab(0);
        }
    });
    history->addWidget(m_historyList);

    QWidget *contactsPage = new QWidget(this);
    QVBoxLayout *contacts = new QVBoxLayout(contactsPage);
    contacts->setContentsMargins(160, 20, 160, 20);
    m_contactList = new QListWidget(this);
    m_contactList->setStyleSheet("QListWidget{background:transparent;border:none;outline:none;} QListWidget::item{border:none;} QListWidget::item:selected{background:transparent;}");
    m_contactList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_contactList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_contactList->setSpacing(2);
    populateContactList();
    connect(m_contactList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        const QString number = item->data(Qt::UserRole).toString();
        if (!number.isEmpty()) {
            m_numberEdit->setText(number);
            activateTab(0);
        }
    });
    contacts->addWidget(m_contactList);

    m_tabStack->addWidget(dialPage);
    m_tabStack->addWidget(historyPage);
    m_tabStack->addWidget(contactsPage);

    main->addWidget(m_tabStack, 1);

    m_detailOverlay = new QWidget(central);
    m_detailOverlay->setGeometry(0, 82, 1280, 638);
    m_detailOverlay->setStyleSheet("QWidget{background:url(:/images/inside_background.png) no-repeat center center;}");
    m_detailOverlay->hide();

    auto *detailBackBtn = new QPushButton(m_detailOverlay);
    detailBackBtn->setGeometry(60, 21, 60, 60);
    detailBackBtn->setCursor(Qt::PointingHandCursor);
    detailBackBtn->setStyleSheet(
        "QPushButton{border:none;background:url(:/images/butt_back_up.png) no-repeat;}"
        "QPushButton:hover{background:url(:/images/butt_back_down.png) no-repeat;}"
    );
    connect(detailBackBtn, &QPushButton::clicked, this, &PhoneWindow::hideContactDetail);

    auto *detailHead = new QWidget(m_detailOverlay);
    detailHead->setGeometry(168, 30, 944, 60);
    auto *detailHeadLayout = new QHBoxLayout(detailHead);
    detailHeadLayout->setContentsMargins(0, 0, 0, 0);
    detailHeadLayout->setSpacing(0);

    auto *detailUserWrap = new QWidget(detailHead);
    auto *detailUserLayout = new QHBoxLayout(detailUserWrap);
    detailUserLayout->setContentsMargins(0, 0, 0, 0);
    detailUserLayout->setSpacing(12);
    auto *detailUserIcon = new QLabel(detailUserWrap);
    detailUserIcon->setPixmap(QPixmap(":/images/pict_callinglist_user.png").scaled(60, 60, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    m_detailNameLabel = new QLabel(QStringLiteral("张三"), detailUserWrap);
    m_detailNameLabel->setStyleSheet("QLabel{color:#fff;font-size:32px;background:transparent;}");
    m_detailNumberLabel = new QLabel(QStringLiteral("18800001234"), detailUserWrap);
    m_detailNumberLabel->setStyleSheet("QLabel{color:#fff;font-size:24px;background:transparent;}");
    detailUserLayout->addWidget(detailUserIcon);
    detailUserLayout->addWidget(m_detailNameLabel);
    detailUserLayout->addWidget(m_detailNumberLabel);

    auto *detailCallBtn = new QPushButton(detailHead);
    detailCallBtn->setFixedSize(60, 60);
    detailCallBtn->setCursor(Qt::PointingHandCursor);
    detailCallBtn->setStyleSheet(
        "QPushButton{border:none;background:url(:/images/butt_calllinglist_answer_up.png) no-repeat center center;}"
        "QPushButton:hover{background-image:url(:/images/butt_calllinglist_answer_down.png);}"
    );
    connect(detailCallBtn, &QPushButton::clicked, this, [this]() {
        if (m_numberEdit && m_detailNumberLabel) {
            m_numberEdit->setText(m_detailNumberLabel->text());
        }
        hideContactDetail();
        showCallOverlay(false);
    });

    detailHeadLayout->addWidget(detailUserWrap);
    detailHeadLayout->addStretch();
    detailHeadLayout->addWidget(detailCallBtn);

    auto *detailListWrap = new QWidget(m_detailOverlay);
    detailListWrap->setGeometry(168, 120, 944, 500);
    auto *detailListLayout = new QVBoxLayout(detailListWrap);
    detailListLayout->setContentsMargins(0, 0, 0, 0);
    detailListLayout->setSpacing(2);
    detailListLayout->addWidget(createDetailLogRow(QStringLiteral("2026.01.22 23:26"), QStringLiteral("00:02:04"), QStringLiteral(":/images/pict_callinglist_state_1.png")));
    detailListLayout->addWidget(createDetailLogRow(QStringLiteral("2026.01.22 23:26"), QStringLiteral("00:02:04"), QStringLiteral(":/images/pict_callinglist_state_2.png")));
    detailListLayout->addWidget(createDetailLogRow(QStringLiteral("2026.01.22 23:26"), QStringLiteral("00:02:04"), QStringLiteral(":/images/pict_callinglist_state_3.png")));
    detailListLayout->addWidget(createDetailLogRow(QStringLiteral("2026.01.22 23:26"), QStringLiteral("00:02:04"), QStringLiteral(":/images/pict_callinglist_state_1.png")));
    detailListLayout->addWidget(createDetailLogRow(QStringLiteral("2026.01.22 23:26"), QStringLiteral("00:02:04"), QStringLiteral(":/images/pict_callinglist_state_1.png")));
    detailListLayout->addStretch();

    m_callOverlay = new QWidget(central);
    m_callOverlay->setObjectName("callOverlay");
    m_callOverlay->setGeometry(40, 90, 1200, 610);
    m_callOverlay->setStyleSheet("QWidget#callOverlay{background:transparent;}");
    m_callOverlay->hide();

    auto *callTop = new QWidget(m_callOverlay);
    callTop->setGeometry(0, 8, 1200, 180);
    callTop->setStyleSheet("background:rgba(255,255,255,0.1);border:1px solid #0068FF;border-radius:5px;");

    auto *callBtnWrap = new QWidget(callTop);
    callBtnWrap->setGeometry(60, 23, 240, 132);
    auto *callBtnLayout = new QHBoxLayout(callBtnWrap);
    callBtnLayout->setContentsMargins(0, 0, 0, 0);
    callBtnLayout->setSpacing(72);

    QPushButton *hangup = new QPushButton(QStringLiteral("挂断"), callBtnWrap);
    hangup->setFixedSize(84, 132);
    hangup->setCursor(Qt::PointingHandCursor);
    hangup->setStyleSheet("QPushButton{border:none;color:#fff;font-size:32px;background:url(:/images/butt_calling_hangup_up.png) no-repeat top center;padding-top:100px;}"
                            "QPushButton:hover{background-image:url(:/images/butt_calling_hangup_down.png);}");
    connect(hangup, &QPushButton::clicked, this, &PhoneWindow::onHangup);

    m_answerButton = new QPushButton(QStringLiteral("接听"), callBtnWrap);
    m_answerButton->setFixedSize(84, 132);
    m_answerButton->setCursor(Qt::PointingHandCursor);
    m_answerButton->setStyleSheet("QPushButton{border:none;color:#fff;font-size:32px;background:url(:/images/butt_calling_anwer_up.png) no-repeat top center;padding-top:100px;}"
                                  "QPushButton:hover{background-image:url(:/images/butt_calling_anwer_down.png);}");
    connect(m_answerButton, &QPushButton::clicked, this, [this]() { updateCallPanel(false); });

    callBtnLayout->addWidget(hangup);
    callBtnLayout->addWidget(m_answerButton);

    auto *userAvatar = new QLabel(callTop);
    userAvatar->setGeometry(652, 23, 132, 132);
    userAvatar->setPixmap(QPixmap(":/images/pic_calling_user.png").scaled(132, 132, Qt::KeepAspectRatio, Qt::SmoothTransformation));

    auto *msgWrap = new QWidget(callTop);
    msgWrap->setGeometry(808, 40, 296, 100);
    m_callNumber = new QLabel(QStringLiteral("18800001234"), msgWrap);
    m_callNumber->setGeometry(0, 0, 296, 48);
    m_callNumber->setStyleSheet("QLabel{color:#fff;font-size:48px;font-weight:bold;background:transparent;}");
    m_callStateLabel = new QLabel(QStringLiteral("通话中..."), msgWrap);
    m_callStateLabel->setGeometry(0, 68, 160, 30);
    m_callStateLabel->setStyleSheet("QLabel{color:#fff;font-size:30px;background:transparent;}");
    m_callTimer = new QLabel(QStringLiteral("03:12"), msgWrap);
    m_callTimer->setGeometry(184, 68, 112, 30);
    m_callTimer->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_callTimer->setStyleSheet("QLabel{color:#fff;font-size:30px;background:transparent;}");

    auto *callingBg = new QLabel(m_callOverlay);
    callingBg->setGeometry(0, 188, 1200, 442);
    callingBg->setPixmap(QPixmap(":/images/pict_calling_phone_bg.png").scaled(1200, 442, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));

    auto *bottomActions = new QWidget(m_callOverlay);
    bottomActions->setGeometry(324, 262, 552, 182);
    auto *bottomLayout = new QHBoxLayout(bottomActions);
    bottomLayout->setContentsMargins(0, 0, 0, 0);
    bottomLayout->setSpacing(92);

    auto makeActionBtn = [this, bottomActions](const QString &text, const QString &up, const QString &down) {
        auto *btn = new QPushButton(text, bottomActions);
        btn->setFixedSize(84, 132);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet(QString("QPushButton{border:none;color:#fff;font-size:32px;background:url(%1) no-repeat top center;padding-top:100px;}"
                                    "QPushButton:hover{background-image:url(%2);}").arg(up, down));
        return btn;
    };

    auto *muteBtn = makeActionBtn(QStringLiteral("静音"), QStringLiteral(":/images/butt_calling_mute_up.png"), QStringLiteral(":/images/butt_calling_mute_down.png"));
    auto *keyboardBtn = makeActionBtn(QStringLiteral("键盘"), QStringLiteral(":/images/butt_calling_keyboard_up.png"), QStringLiteral(":/images/butt_calling_keyboard_down.png"));
    auto *recordBtn = makeActionBtn(QStringLiteral("录音"), QStringLiteral(":/images/butt_calling_recording_up.png"), QStringLiteral(":/images/butt_calling_recording_down.png"));
    connect(keyboardBtn, &QPushButton::clicked, this, [this]() {
        if (m_callKeyboardPanel) {
            m_callKeyboardPanel->setVisible(!m_callKeyboardPanel->isVisible());
        }
    });
    bottomLayout->addWidget(muteBtn);
    bottomLayout->addWidget(keyboardBtn);
    bottomLayout->addWidget(recordBtn);

    m_callKeyboardPanel = new QWidget(m_callOverlay);
    m_callKeyboardPanel->setGeometry(232, 188, 736, 414);
    m_callKeyboardPanel->hide();
    auto *keyboardWrap = new QWidget(m_callKeyboardPanel);
    keyboardWrap->setGeometry(63, 0, 610, 414);
    auto *keyboardGrid = new QGridLayout(keyboardWrap);
    keyboardGrid->setContentsMargins(0, 0, 0, 0);
    keyboardGrid->setHorizontalSpacing(8);
    keyboardGrid->setVerticalSpacing(8);
    const QStringList inCallKeys = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "*", "0", "#"};
    const QStringList inCallUp = {
        ":/images/butt_calling_num1_up.png", ":/images/butt_calling_num2_up.png", ":/images/butt_calling_num3_up.png",
        ":/images/butt_calling_num4_up.png", ":/images/butt_calling_num5_up.png", ":/images/butt_calling_num6_up.png",
        ":/images/butt_calling_num7_up.png", ":/images/butt_calling_num8_up.png", ":/images/butt_calling_num9_up.png",
        ":/images/butt_calling_num10_up.png", ":/images/butt_calling_num0_up.png", ":/images/butt_calling_num12_up.png"
    };
    const QStringList inCallDown = {
        ":/images/butt_calling_num1_down.png", ":/images/butt_calling_num2_down.png", ":/images/butt_calling_num3_down.png",
        ":/images/butt_calling_num4_down.png", ":/images/butt_calling_num5_down.png", ":/images/butt_calling_num6_down.png",
        ":/images/butt_calling_num7_down.png", ":/images/butt_calling_num8_down.png", ":/images/butt_calling_num9_down.png",
        ":/images/butt_calling_num10_down.png", ":/images/butt_calling_num0_down.png", ":/images/butt_calling_num12_down.png"
    };
    for (int i = 0; i < inCallKeys.size(); ++i) {
        auto *btn = new QPushButton(QString(), keyboardWrap);
        btn->setFixedSize(198, 120);
        btn->setStyleSheet(QString("QPushButton{color:transparent;font-size:64px;font-weight:bold;border:none;background:url(%1) no-repeat center center;}"
                                    "QPushButton:hover{background-image:url(%2);}").arg(inCallUp.at(i), inCallDown.at(i)));
        connect(btn, &QPushButton::clicked, this, [this, btn]() { appendDigit(btn->text()); });
        keyboardGrid->addWidget(btn, i / 3, i % 3);
    }
    auto *hideKeyboardBtn = new QPushButton(QStringLiteral("隐藏"), m_callKeyboardPanel);
    hideKeyboardBtn->setGeometry(681, 87, 54, 240);
    hideKeyboardBtn->setStyleSheet("QPushButton{border:none;background:#0068FF;color:#fff;font-size:48px;font-weight:bold;}"
                                   "QPushButton:hover{background:#00FAFF;}");
    connect(hideKeyboardBtn, &QPushButton::clicked, this, [this]() {
        if (m_callKeyboardPanel) {
            m_callKeyboardPanel->hide();
        }
    });

    auto timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [this]() {
        if (!m_callOverlay || !m_callOverlay->isVisible() || !m_callTimer || !m_callStateLabel) {
            return;
        }
        if (m_callStateLabel->text() != QStringLiteral("通话中...")) {
            return;
        }
        const QTime t = QTime::fromString(m_callTimer->text(), "mm:ss");
        const QTime n = t.addSecs(1);
        m_callTimer->setText(n.toString("mm:ss"));
    });
    timer->start(1000);
}

void PhoneWindow::appendDigit(const QString &text) {
    if (!m_numberEdit) {
        return;
    }
    m_numberEdit->setText(m_numberEdit->text() + text);
}

void PhoneWindow::activateTab(int index) {
    if (!m_tabStack) {
        return;
    }
    m_tabStack->setCurrentIndex(index);

    const QString leftOn = "QPushButton{border:none;color:#fff;font-size:28px;background:url(:/images/butt_tab_left_on.png) no-repeat center center;}";
    const QString leftOff = "QPushButton{border:none;color:#fff;font-size:28px;background:url(:/images/butt_tab_left_down.png) no-repeat center center;} QPushButton:hover{color:#00FAFF;}";
    const QString centerOn = "QPushButton{border:none;color:#fff;font-size:28px;background:url(:/images/butt_tab_center_on.png) no-repeat center center;}";
    const QString centerOff = "QPushButton{border:none;color:#fff;font-size:28px;background:url(:/images/butt_tab_center_down.png) no-repeat center center;} QPushButton:hover{color:#00FAFF;}";
    const QString rightOn = "QPushButton{border:none;color:#fff;font-size:28px;background:url(:/images/butt_tab_right_on.png) no-repeat center center;}";
    const QString rightOff = "QPushButton{border:none;color:#fff;font-size:28px;background:url(:/images/butt_tab_right_down.png) no-repeat center center;} QPushButton:hover{color:#00FAFF;}";
    m_tabDial->setStyleSheet(index == 0 ? leftOn : leftOff);
    m_tabHistory->setStyleSheet(index == 1 ? centerOn : centerOff);
    m_tabContacts->setStyleSheet(index == 2 ? rightOn : rightOff);
}

void PhoneWindow::onDial() {
    showCallOverlay(false);
}

void PhoneWindow::onHangup() {
    if (m_callOverlay) {
        m_callOverlay->hide();
    }
    if (m_callKeyboardPanel) {
        m_callKeyboardPanel->hide();
    }
    if (m_historyList && m_callNumber) {
        auto *item = new QListWidgetItem(m_historyList);
        item->setData(Qt::UserRole, m_callNumber->text());
        item->setSizeHint(QSize(944, 68));
        m_historyList->insertItem(0, item);
        m_historyList->setItemWidget(item, createHistoryRow(QStringLiteral("最近通话"), m_callNumber->text(), QDateTime::currentDateTime().toString("yyyy.MM.dd  " + AppSignals::timeFormat()), QStringLiteral(":/images/pict_callinglist_state_1.png"), true));
    }
}

void PhoneWindow::onDialTab() {
    activateTab(0);
}

void PhoneWindow::onHistoryTab() {
    activateTab(1);
}

void PhoneWindow::onContactsTab() {
    activateTab(2);
}

void PhoneWindow::populateHistoryList() {
    if (!m_historyList) {
        return;
    }

    const struct RowData {
        QString name;
        QString number;
        QString time;
        QString icon;
    } rows[] = {
        {QStringLiteral("张三"), QStringLiteral("18800001234"), QStringLiteral("2026.01.22  23:26"), QStringLiteral(":/images/pict_callinglist_state_1.png")},
        {QStringLiteral("张三"), QStringLiteral("18800001234"), QStringLiteral("2026.01.22  23:26"), QStringLiteral(":/images/pict_callinglist_state_2.png")},
        {QStringLiteral("张三"), QStringLiteral("18800001234"), QStringLiteral("2026.01.22  23:26"), QStringLiteral(":/images/pict_callinglist_state_3.png")},
        {QStringLiteral("张三"), QStringLiteral("18800001234"), QStringLiteral("2026.01.22  23:26"), QStringLiteral(":/images/pict_callinglist_state_1.png")}
    };

    for (const RowData &row : rows) {
        auto *item = new QListWidgetItem(m_historyList);
        item->setData(Qt::UserRole, row.number);
        item->setSizeHint(QSize(944, 68));
        m_historyList->addItem(item);
        m_historyList->setItemWidget(item, createHistoryRow(row.name, row.number, row.time, row.icon, true));
    }
}

void PhoneWindow::populateContactList() {
    if (!m_contactList) {
        return;
    }

    const QList<QPair<QString, QString>> rows = {
        {QStringLiteral("张三"), QStringLiteral("18800001234")},
        {QStringLiteral("李四"), QStringLiteral("13600001234")},
        {QStringLiteral("王五"), QStringLiteral("13900001111")},
        {QStringLiteral("赵六"), QStringLiteral("13700002222")}
    };

    for (const auto &row : rows) {
        auto *item = new QListWidgetItem(m_contactList);
        item->setData(Qt::UserRole, row.second);
        item->setSizeHint(QSize(944, 68));
        m_contactList->addItem(item);
        m_contactList->setItemWidget(item, createContactRow(row.first, row.second));
    }
}

QWidget *PhoneWindow::createHistoryRow(const QString &name, const QString &number, const QString &timeText, const QString &stateIcon, bool detailButton) {
    auto *row = new QWidget();
    row->setFixedSize(944, 68);
    row->setStyleSheet("QWidget{background:rgba(255,255,255,0.1);border-radius:34px;}");

    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(24, 4, 24, 4);
    layout->setSpacing(12);

    auto *userWrap = new QWidget(row);
    auto *userLayout = new QHBoxLayout(userWrap);
    userLayout->setContentsMargins(0, 0, 0, 0);
    userLayout->setSpacing(24);
    auto *userIcon = new QLabel(userWrap);
    userIcon->setPixmap(QPixmap(":/images/pict_callinglist_user.png").scaled(52, 52, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    auto *userLabel = new QLabel(name, userWrap);
    userLabel->setStyleSheet("QLabel{color:#fff;font-size:32px;background:transparent;}");
    userLayout->addWidget(userIcon);
    userLayout->addWidget(userLabel);

    auto *numWrap = new QWidget(row);
    auto *numLayout = new QHBoxLayout(numWrap);
    numLayout->setContentsMargins(0, 0, 0, 0);
    numLayout->setSpacing(16);
    auto *state = new QLabel(numWrap);
    state->setPixmap(QPixmap(stateIcon).scaled(36, 36, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    auto *numLabel = new QLabel(number, numWrap);
    numLabel->setStyleSheet("QLabel{color:#fff;font-size:24px;background:transparent;}");
    numLayout->addWidget(state);
    numLayout->addWidget(numLabel);

    auto *timeBtn = new QPushButton(timeText, row);
    timeBtn->setCursor(Qt::PointingHandCursor);
    timeBtn->setStyleSheet(detailButton
                               ? "QPushButton{border:none;color:#fff;font-size:24px;padding-right:60px;background:url(:/images/butt_callinglist_detail_up.png) no-repeat right center;}QPushButton:hover{background-image:url(:/images/butt_callinglist_detail_down.png);}"
                               : "QPushButton{border:none;color:#fff;font-size:24px;background:transparent;}");
    connect(timeBtn, &QPushButton::clicked, this, [this, name, number]() {
        showContactDetail(name, number);
    });

    layout->addWidget(userWrap);
    layout->addStretch();
    layout->addWidget(numWrap);
    layout->addStretch();
    layout->addWidget(timeBtn);
    return row;
}

QWidget *PhoneWindow::createContactRow(const QString &name, const QString &number) {
    auto *row = new QWidget();
    row->setFixedSize(944, 68);
    row->setStyleSheet("QWidget{background:rgba(255,255,255,0.1);border-radius:34px;}");

    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(24, 4, 24, 4);
    layout->setSpacing(12);

    auto *userWrap = new QWidget(row);
    auto *userLayout = new QHBoxLayout(userWrap);
    userLayout->setContentsMargins(0, 0, 0, 0);
    userLayout->setSpacing(24);
    auto *userIcon = new QLabel(userWrap);
    userIcon->setPixmap(QPixmap(":/images/pict_callinglist_user.png").scaled(52, 52, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    auto *nameLabel = new QLabel(name, userWrap);
    nameLabel->setStyleSheet("QLabel{color:#fff;font-size:32px;background:transparent;}");
    userLayout->addWidget(userIcon);
    userLayout->addWidget(nameLabel);

    auto *numLabel = new QLabel(number, row);
    numLabel->setStyleSheet("QLabel{color:#fff;font-size:24px;background:transparent;}");

    auto *callBtn = new QPushButton(row);
    callBtn->setFixedSize(60, 60);
    callBtn->setCursor(Qt::PointingHandCursor);
    callBtn->setStyleSheet("QPushButton{border:none;background:url(:/images/butt_calllinglist_answer_up.png) no-repeat right center;}"
                           "QPushButton:hover{background:url(:/images/butt_calllinglist_answer_down.png) no-repeat right center;}");
    connect(callBtn, &QPushButton::clicked, this, [this, number]() {
        if (m_numberEdit) {
            m_numberEdit->setText(number);
        }
        showCallOverlay(false);
    });

    layout->addWidget(userWrap);
    layout->addStretch();
    layout->addWidget(numLabel);
    layout->addStretch();
    layout->addWidget(callBtn);
    return row;
}

void PhoneWindow::showCallOverlay(bool incoming) {
    if (m_numberEdit && m_callNumber) {
        m_callNumber->setText(m_numberEdit->text());
    }
    updateCallPanel(incoming);
    if (m_callKeyboardPanel) {
        m_callKeyboardPanel->hide();
    }
    if (m_callOverlay) {
        m_callOverlay->show();
        m_callOverlay->raise();
    }
}

void PhoneWindow::updateCallPanel(bool incoming) {
    if (!m_callStateLabel || !m_callTimer || !m_answerButton) {
        return;
    }

    if (incoming) {
        m_callStateLabel->setText(QStringLiteral("正在呼入..."));
        m_callTimer->hide();
        m_answerButton->show();
    } else {
        m_callStateLabel->setText(QStringLiteral("通话中..."));
        m_callTimer->setText(QStringLiteral("00:00"));
        m_callTimer->show();
        m_answerButton->hide();
    }
}

QWidget *PhoneWindow::createDetailLogRow(const QString &timeText, const QString &durationText, const QString &stateIcon) {
    auto *row = new QWidget();
    row->setFixedSize(944, 68);
    row->setStyleSheet("QWidget{background:rgba(255,255,255,0.1);}");

    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(24, 4, 24, 4);
    layout->setSpacing(12);

    auto *leftWrap = new QWidget(row);
    auto *leftLayout = new QHBoxLayout(leftWrap);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(16);
    auto *icon = new QLabel(leftWrap);
    icon->setPixmap(QPixmap(stateIcon).scaled(36, 36, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    auto *timeLabel = new QLabel(timeText, leftWrap);
    timeLabel->setStyleSheet("QLabel{color:#fff;font-size:24px;background:transparent;}");
    leftLayout->addWidget(icon);
    leftLayout->addWidget(timeLabel);

    auto *durationLabel = new QLabel(durationText, row);
    durationLabel->setStyleSheet("QLabel{color:#fff;font-size:24px;background:transparent;}");

    layout->addWidget(leftWrap);
    layout->addStretch();
    layout->addWidget(durationLabel);
    return row;
}

void PhoneWindow::showContactDetail(const QString &name, const QString &number) {
    if (m_detailNameLabel) {
        m_detailNameLabel->setText(name);
    }
    if (m_detailNumberLabel) {
        m_detailNumberLabel->setText(number);
    }
    if (m_detailOverlay) {
        m_detailOverlay->show();
        m_detailOverlay->raise();
    }
}

void PhoneWindow::hideContactDetail() {
    if (m_detailOverlay) {
        m_detailOverlay->hide();
    }
}

void PhoneWindow::keyPressEvent(QKeyEvent *event)
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
        close();
        break;
    case Qt::Key_Back:
    case Qt::Key_Escape:
        if (m_detailOverlay && m_detailOverlay->isVisible()) {
            hideContactDetail();
        } else {
            emit requestReturnToMain();
            close();
        }
        break;
    default:
        QMainWindow::keyPressEvent(event);
    }
}
