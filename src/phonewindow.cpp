#include "phonewindow.h"
#include "bluetoothmanager.h"
#include "topbarwidget.h"
#include "appsignals.h"

#include <algorithm>
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

QString PhoneWindow::s_cachedDialNumber;
QList<QPair<QString, QString>> PhoneWindow::s_cachedContactEntries;

PhoneWindow::PhoneWindow(BluetoothManager *bluetoothManager, QWidget *parent)
    : QMainWindow(parent)
    , m_bluetoothManager(bluetoothManager)
    , m_tabStack(nullptr)
    , m_tabDial(nullptr)
    , m_tabHistory(nullptr)
    , m_tabContacts(nullptr)
    , m_topBar(nullptr)
    , m_numberEdit(nullptr)
    , m_previousTabIndex(0)
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
    , m_bottomActions(nullptr)
    , m_answerButton(nullptr)
{
    setWindowTitle("蓝牙电话");
    setFixedSize(1280, 720);
    if (QApplication::primaryScreen()) {
        move(QApplication::primaryScreen()->geometry().center() - rect().center());
    }
    if (!s_cachedContactEntries.isEmpty()) {
        m_contactEntries = s_cachedContactEntries;
    }
    setupUI();
    activateTab(0);
}

void PhoneWindow::closeEvent(QCloseEvent *event) {
    emit requestReturnToMain();
    hide();
    event->ignore();
}

void PhoneWindow::setupUI() {
    QWidget *central = new QWidget(this);
    central->setStyleSheet("background-image:url(:/images/inside_background.png); background-repeat:no-repeat;");
    setCentralWidget(central);

    QVBoxLayout *main = new QVBoxLayout(central);
    main->setContentsMargins(0, 82, 0, 0);
    main->setSpacing(0);

    m_topBar = new QWidget(this);
    m_topBar->setGeometry(0, 0, 1280, 82);
    m_topBar->setStyleSheet("background-image:url(:/images/topbar.png);");
    QGridLayout *top = new QGridLayout(m_topBar);
    top->setContentsMargins(16, 0, 16, 0);
    top->setColumnStretch(0, 1);
    top->setColumnStretch(1, 0);
    top->setColumnStretch(2, 1);

    QPushButton *homeBtn = new QPushButton(m_topBar);
    homeBtn->setFixedSize(48, 48);
    homeBtn->setStyleSheet("QPushButton{border:none;background-image:url(:/images/pict_home_up.png);} QPushButton:hover{background-image:url(:/images/pict_home_down.png);}");
    homeBtn->setFocusPolicy(Qt::NoFocus);
    connect(homeBtn, &QPushButton::clicked, this, [this](){ emit requestReturnToMain(); close(); });
    top->addWidget(homeBtn, 0, 0, Qt::AlignLeft | Qt::AlignVCenter);

    QLabel *title = new QLabel("蓝牙电话", m_topBar);
    title->setStyleSheet("color:#fff;font-size:36px;font-weight:bold;background:transparent;");
    top->addWidget(title, 0, 1, Qt::AlignCenter);

    auto *topBarRight = new TopBarRightWidget(m_topBar);
    top->addWidget(topBarRight, 0, 2, Qt::AlignRight | Qt::AlignVCenter);

    QWidget *tabWrap = new QWidget(central);
    tabWrap->setFixedHeight(84);
    QHBoxLayout *tabs = new QHBoxLayout(tabWrap);
    tabs->setContentsMargins(396, 18, 396, 0);
    tabs->setSpacing(1);

    auto mkTab = [this](const QString &text) {
        QPushButton *b = new QPushButton(text, this);
        b->setFixedSize(160, 66);
        b->setCursor(Qt::PointingHandCursor);
        b->setStyleSheet("QPushButton{border:none;color:#fff;font-size:28px;background-repeat:no-repeat;background-position:center;} QPushButton:hover{color:#00FAFF;}");
        b->setFocusPolicy(Qt::NoFocus);
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

    m_tabWrap = tabWrap;
    main->addWidget(tabWrap);

    if (m_bluetoothManager) {
        connect(m_bluetoothManager, &BluetoothManager::callStatusChanged, this, &PhoneWindow::onBluetoothCallStatusChanged);
        connect(m_bluetoothManager, &BluetoothManager::callNumberUpdated, this, &PhoneWindow::onBluetoothCallNumberUpdated);
        connect(m_bluetoothManager, &BluetoothManager::phonebookEntryReceived, this, &PhoneWindow::onBluetoothPhonebookEntryReceived);
        connect(m_bluetoothManager, &BluetoothManager::callLogEntryReceived, this, &PhoneWindow::onBluetoothCallLogEntryReceived);
        connect(m_bluetoothManager, &BluetoothManager::phonebookDownloadFinished, this, &PhoneWindow::onBluetoothPhonebookDownloadFinished);
        connect(m_bluetoothManager, &BluetoothManager::callLogDownloadFinished, this, &PhoneWindow::onBluetoothCallLogDownloadFinished);
        connect(m_bluetoothManager, &BluetoothManager::deviceConnected, this, &PhoneWindow::onBluetoothDeviceConnected);

        if (m_bluetoothManager->isConnected()) {
            onBluetoothDeviceConnected(m_bluetoothManager->getConnectedDeviceName());
        }
    }

    m_tabStack = new QStackedWidget(central);

    QWidget *dialPage = new QWidget(central);
    QVBoxLayout *dial = new QVBoxLayout(dialPage);
    dial->setContentsMargins(232, 24, 232, 20);
    dial->setSpacing(8);

    m_numberEdit = new QLineEdit(dialPage);
    m_numberEdit->setPlaceholderText(QStringLiteral("请输入电话号码"));
    m_numberEdit->setFixedSize(816, 72);
    m_numberEdit->setStyleSheet("QLineEdit{color:#fff;font-size:48px;background:rgba(255,255,255,0.1);border:1px solid #0068FF;padding:0 20px;}");
    if (!s_cachedDialNumber.isEmpty()) {
        m_numberEdit->setText(s_cachedDialNumber);
    }
    dial->addWidget(m_numberEdit);

    QWidget *contentWrap = new QWidget(dialPage);
    QHBoxLayout *contentLayout = new QHBoxLayout(contentWrap);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(8);

    QWidget *kb = new QWidget(contentWrap);
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
        QPushButton *key = new QPushButton(kb);
        key->setProperty("digit", keys.at(i));
        key->setFixedSize(198, 94);
        key->setFocusPolicy(Qt::NoFocus);
        key->setStyleSheet(
            QString("QPushButton{border:none;background:url(%1) no-repeat center center;}"
                    "QPushButton:hover{background-image:url(%2);}").arg(upIcons.at(i), downIcons.at(i))
        );
        connect(key, &QPushButton::clicked, this, [this, key]() { appendDigit(key->property("digit").toString()); });
        grid->addWidget(key, i / 3, i % 3);
    }

    contentLayout->addWidget(kb);

    QWidget *action = new QWidget(contentWrap);
    action->setFixedWidth(198);
    QHBoxLayout *actionLayout = new QHBoxLayout(action);
    actionLayout->setContentsMargins(0, 0, 0, 0);
    actionLayout->setSpacing(8);
    actionLayout->setDirection(QBoxLayout::TopToBottom);

    QPushButton *delBtn = new QPushButton(action);
    delBtn->setFixedSize(198, 196);
    delBtn->setStyleSheet("QPushButton{border:none;background:url(:/images/butt_calling_del_up.png) no-repeat center center;}"
                          "QPushButton:hover{background:url(:/images/butt_calling_del_down.png) no-repeat center center;}");
    connect(delBtn, &QPushButton::clicked, this, [this]() {
        if (!m_numberEdit) {
            return;
        }
        const QString text = m_numberEdit->text().left(m_numberEdit->text().size() - 1);
        m_numberEdit->setText(text);
        s_cachedDialNumber = text;
    });
    delBtn->setFocusPolicy(Qt::NoFocus);

    QPushButton *dialBtn = new QPushButton(QStringLiteral("拨号"), action);
    dialBtn->setFixedSize(198, 196);
    dialBtn->setStyleSheet("QPushButton{color:#fff;font-size:48px;font-weight:bold;border:none;background:#0068FF;text-align:center;}"
                           "QPushButton:hover{background:#00FAFF;}");
    connect(dialBtn, &QPushButton::clicked, this, &PhoneWindow::onDial);
    dialBtn->setFocusPolicy(Qt::NoFocus);

    actionLayout->addWidget(delBtn);
    actionLayout->addWidget(dialBtn);

    contentLayout->addWidget(action);
    dial->addWidget(contentWrap, 0, Qt::AlignHCenter);

    QWidget *historyPage = new QWidget(central);
    QVBoxLayout *history = new QVBoxLayout(historyPage);
    history->setContentsMargins(40, 20, 40, 20);
    m_historyList = new QListWidget(historyPage);
    m_historyList->setStyleSheet(
        "QListWidget{background:transparent;border:none;outline:none;padding-right:100px;}"
        "QListWidget::item{border:none;}"
        "QListWidget::item:selected{background:transparent;}"
        "QScrollBar:vertical{width:36px;background:transparent;border-radius:6px;margin:0;padding:0;}"
        "QScrollBar::groove:vertical{background:rgba(0,104,255,0.10);border-radius:3px;margin:0px 3px;padding:0;}"
        "QScrollBar::handle:vertical{background:#0068FF;border-radius:3px;min-height:60px;margin:3px 3px;}"
        "QScrollBar::handle:vertical:hover{background:#00faff;}"
        "QScrollBar::sub-line:vertical, QScrollBar::add-line:vertical{height:0px;background:transparent;border:none;}"
        "QScrollBar::sub-page:vertical, QScrollBar::add-page:vertical{background:transparent;}"
    );
    m_historyList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_historyList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_historyList->setUniformItemSizes(true);
    m_historyList->setStyleSheet(m_historyList->styleSheet() + "QListWidget::item{padding-left:24px;padding-right:24px;}");
    m_historyList->setContentsMargins(30, 20, 30, 20);
    m_historyList->setSpacing(2);
    populateHistoryList();
    connect(m_historyList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        const QString number = item->data(Qt::UserRole).toString();
        if (!number.isEmpty()) {
            cacheDialNumber(number);
            activateTab(0);
        }
    });
    history->addWidget(m_historyList);

    QWidget *contactsPage = new QWidget(central);
    QVBoxLayout *contacts = new QVBoxLayout(contactsPage);
    contacts->setContentsMargins(40, 20, 40, 20);
    m_contactList = new QListWidget(contactsPage);
    m_contactList->setStyleSheet(
        "QListWidget{background:transparent;border:none;outline:none;padding-right:100px;}"
        "QListWidget::item{border:none;}"
        "QListWidget::item:selected{background:transparent;}"
        "QScrollBar:vertical{width:36px;background:transparent;border-radius:6px;margin:0;padding:0;}"
        "QScrollBar::groove:vertical{background:rgba(0,104,255,0.10);border-radius:3px;margin:0px 3px;padding:0;}"
        "QScrollBar::handle:vertical{background:#0068FF;border-radius:3px;min-height:60px;margin:3px 3px;}"
        "QScrollBar::handle:vertical:hover{background:#00faff;}"
        "QScrollBar::sub-line:vertical, QScrollBar::add-line:vertical{height:0px;background:transparent;border:none;}"
        "QScrollBar::sub-page:vertical, QScrollBar::add-page:vertical{background:transparent;}"
    );
    m_contactList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_contactList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_contactList->setUniformItemSizes(true);
    m_contactList->setStyleSheet(m_contactList->styleSheet() + "QListWidget::item{padding-left:24px;padding-right:24px;}");
    m_contactList->setContentsMargins(30, 20, 30, 20);
    m_contactList->setSpacing(2);
    populateContactList();
    connect(m_contactList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        const QString number = item->data(Qt::UserRole).toString();
        if (!number.isEmpty()) {
            cacheDialNumber(number);
            activateTab(0);
        }
    });
    contacts->addWidget(m_contactList);

    m_tabStack->addWidget(dialPage);
    m_tabStack->addWidget(historyPage);
    m_tabStack->addWidget(contactsPage);

    main->addWidget(m_tabStack, 1);

    m_detailOverlay = new QWidget(this);
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
        if (m_detailNumberLabel) {
            const QString number = m_detailNumberLabel->text();
            cacheDialNumber(number);
            if (m_bluetoothManager) {
                m_bluetoothManager->dialNumber(number);
            }
        }
        hideContactDetail();
    });

    detailHeadLayout->addWidget(detailUserWrap);
    detailHeadLayout->addStretch();
    detailHeadLayout->addWidget(detailCallBtn);

    auto *detailListWrap = new QWidget(m_detailOverlay);
    detailListWrap->setGeometry(168, 156, 944, 500);
    m_detailListLayout = new QVBoxLayout(detailListWrap);
    m_detailListLayout->setContentsMargins(0, 0, 0, 0);
    m_detailListLayout->setSpacing(2);
    m_detailListLayout->addStretch();

    m_callOverlay = new QWidget(this);
    m_callOverlay->setObjectName("callOverlay");
    m_callOverlay->setStyleSheet("QWidget#callOverlay{background:transparent;}");
    m_callOverlay->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto *callPageLayout = new QVBoxLayout(m_callOverlay);
    callPageLayout->setContentsMargins(40, 8, 40, 20);
    callPageLayout->setSpacing(0);

    auto *callTop = new QWidget(m_callOverlay);
    callTop->setFixedHeight(180);
    callTop->setStyleSheet("background:#000000;border:1px solid #0068FF;border-radius:5px;");

    auto *callBtnWrap = new QWidget(callTop);
    callBtnWrap->setGeometry(60, 23, 240, 132);
    callBtnWrap->setStyleSheet("border:none;background:transparent;");
    auto *callBtnLayout = new QHBoxLayout(callBtnWrap);
    callBtnLayout->setContentsMargins(0, 0, 0, 0);
    callBtnLayout->setSpacing(72);

    QPushButton *hangup = new QPushButton(QStringLiteral("挂断"), callBtnWrap);
    hangup->setFixedSize(84, 132);
    hangup->setCursor(Qt::PointingHandCursor);
    hangup->setFocusPolicy(Qt::NoFocus);
    hangup->setFlat(true);
    hangup->setStyleSheet("QPushButton{border:none;outline:none;background:transparent;color:#fff;font-size:32px;background-image:url(:/images/butt_calling_hangup_up.png);background-repeat:no-repeat;background-position:top center;padding-top:100px;}" 
                         "QPushButton:hover{background-image:url(:/images/butt_calling_hangup_down.png);}" 
                         "QPushButton:focus{border:none;outline:none;}");
    connect(hangup, &QPushButton::clicked, this, &PhoneWindow::onHangup);

    m_answerButton = new QPushButton(QStringLiteral("接听"), callBtnWrap);
    m_answerButton->setFixedSize(84, 132);
    m_answerButton->setCursor(Qt::PointingHandCursor);
    m_answerButton->setFocusPolicy(Qt::NoFocus);
    m_answerButton->setFlat(true);
    m_answerButton->setStyleSheet("QPushButton{border:none;outline:none;background:transparent;color:#fff;font-size:32px;background-image:url(:/images/butt_calling_anwer_up.png);background-repeat:no-repeat;background-position:top center;padding-top:100px;}"
                                  "QPushButton:hover{background-image:url(:/images/butt_calling_anwer_down.png);}" 
                                  "QPushButton:focus{border:none;outline:none;}");
    connect(m_answerButton, &QPushButton::clicked, this, &PhoneWindow::onAnswer);

    callBtnLayout->addWidget(hangup);
    callBtnLayout->addWidget(m_answerButton);

    auto *userAvatar = new QLabel(callTop);
    userAvatar->setGeometry(652, 23, 132, 132);
    userAvatar->setStyleSheet("border:none;background:transparent;");
    userAvatar->setPixmap(QPixmap(":/images/pic_calling_user.png").scaled(132, 132, Qt::KeepAspectRatio, Qt::SmoothTransformation));

    auto *msgWrap = new QWidget(callTop);
    msgWrap->setGeometry(808, 40, 296, 100);
    msgWrap->setStyleSheet("border:none;background:transparent;");
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

    auto *bottomActions = new QWidget(m_callOverlay);
    bottomActions->setFixedHeight(182);
    m_bottomActions = bottomActions;
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
    connect(keyboardBtn, &QPushButton::clicked, this, [this, bottomActions]() {
        if (!m_callKeyboardPanel) {
            return;
        }
        const bool visible = !m_callKeyboardPanel->isVisible();
        m_callKeyboardPanel->setVisible(visible);
        if (bottomActions) {
            bottomActions->setVisible(!visible);
        }
    });
    bottomLayout->addWidget(muteBtn);
    bottomLayout->addWidget(keyboardBtn);
    bottomLayout->addWidget(recordBtn);

    callPageLayout->addWidget(callTop);
    callPageLayout->addSpacing(82);
    callPageLayout->addWidget(bottomActions, 0, Qt::AlignHCenter);
    callPageLayout->addStretch();

    m_callKeyboardPanel = new QWidget(m_callOverlay);
    m_callKeyboardPanel->setGeometry(232, 188, 816, 414);
    m_callKeyboardPanel->hide();
    auto *keyboardWrap = new QWidget(m_callKeyboardPanel);
    keyboardWrap->setGeometry(0, 0, 610, 414);
    keyboardWrap->setStyleSheet("background:transparent;");
    auto *keyboardGrid = new QGridLayout(keyboardWrap);
    keyboardGrid->setContentsMargins(0, 0, 0, 0);
    keyboardGrid->setHorizontalSpacing(8);
    keyboardGrid->setVerticalSpacing(8);
    const QStringList inCallKeys = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "*", "0", "#"};
    const QStringList inCallLetters = {"", "ABC", "DEF", "GHI", "JKL", "MNO", "PQRS", "TUV", "WXYZ", "", "+", ""};
    for (int i = 0; i < inCallKeys.size(); ++i) {
        auto *btn = new QPushButton(QString(), keyboardWrap);
        btn->setProperty("digit", inCallKeys.at(i));
        const QString label = inCallKeys.at(i);
        const QString letters = inCallLetters.at(i);
        btn->setFixedSize(198, 94);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFocusPolicy(Qt::NoFocus);
        btn->setStyleSheet(
            "QPushButton{border:1px solid #0068FF;color:#fff;background:transparent;font-weight:bold;}"
            "QPushButton:hover{border:1px solid #00FAFF;color:#00FAFF;background:transparent;}"
            "QPushButton:pressed{background:transparent;}"
            "QPushButton:focus{outline:none;}");

        auto *btnLayout = new QVBoxLayout(btn);
        btnLayout->setContentsMargins(0, 0, 0, 0);
        btnLayout->setSpacing(0);

        auto *contentWrap = new QWidget(btn);
        contentWrap->setFixedSize(198, 94);
        contentWrap->setStyleSheet("background:transparent;");
        contentWrap->setAttribute(Qt::WA_TranslucentBackground);
        auto *contentLayout = new QVBoxLayout(contentWrap);
        contentLayout->setContentsMargins(0, 0, 0, 0);
        contentLayout->setSpacing(!letters.isEmpty() ? 2 : 0);
        contentLayout->addStretch();

        auto *digitLabel = new QLabel(label, contentWrap);
        const bool isLargeSymbol = (label == "*" || label == "#");
        digitLabel->setStyleSheet(QString("color:#fff;font-size:%1px;font-weight:bold;background:transparent;")
                                  .arg(isLargeSymbol ? 64 : 48));
        digitLabel->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
        digitLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
        contentLayout->addWidget(digitLabel, 0, Qt::AlignHCenter | Qt::AlignTop);

        if (!letters.isEmpty()) {
            auto *lettersLabel = new QLabel(letters, contentWrap);
            lettersLabel->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
            lettersLabel->setStyleSheet("color:#fff;font-size:22px;font-weight:normal;background:transparent;");
            lettersLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
            contentLayout->addWidget(lettersLabel, 0, Qt::AlignHCenter | Qt::AlignTop);
            contentLayout->addStretch();
        }

        btnLayout->addStretch();
        btnLayout->addWidget(contentWrap, 0, Qt::AlignHCenter);
        btnLayout->addStretch();

        connect(btn, &QPushButton::clicked, this, [this, btn]() { appendDigit(btn->property("digit").toString()); });
        keyboardGrid->addWidget(btn, i / 3, i % 3);
    }
    auto *hideKeyboardBtn = new QPushButton(QStringLiteral("隐藏"), m_callKeyboardPanel);
    hideKeyboardBtn->setGeometry(618, 7, 198, 400);
    hideKeyboardBtn->setCursor(Qt::PointingHandCursor);
    hideKeyboardBtn->setFocusPolicy(Qt::NoFocus);
    hideKeyboardBtn->setStyleSheet("QPushButton{border:none;background:#0068FF;color:#fff;font-size:48px;font-weight:bold;}"
                                   "QPushButton:hover{background:#00FAFF;}");
    connect(hideKeyboardBtn, &QPushButton::clicked, this, [this, bottomActions]() {
        if (m_callKeyboardPanel) {
            m_callKeyboardPanel->hide();
        }
        if (bottomActions) {
            bottomActions->show();
        }
    });

    m_tabStack->addWidget(m_callOverlay);

    auto timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [this]() {
        if (!m_callOverlay || !m_callTimer || !m_callStateLabel) {
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
    const QString newText = m_numberEdit->text() + text;
    m_numberEdit->setText(newText);
    s_cachedDialNumber = newText;
}

void PhoneWindow::cacheDialNumber(const QString &number) {
    if (m_numberEdit) {
        m_numberEdit->setText(number);
    }
    s_cachedDialNumber = number;
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

    if (m_numberEdit) {
        if (index == 0) {
            m_numberEdit->show();
        } else {
            m_numberEdit->hide();
        }
    }
}

void PhoneWindow::onDial() {
    if (m_bluetoothManager && m_numberEdit) {
        const QString number = m_numberEdit->text().trimmed();
        if (!number.isEmpty()) {
            m_bluetoothManager->dialNumber(number);
        }
    }
}

void PhoneWindow::onAnswer() {
    if (m_bluetoothManager) {
        m_bluetoothManager->answerCall();
    }
    updateCallPanel(false);
}

void PhoneWindow::onHangup() {
    if (m_bluetoothManager) {
        m_bluetoothManager->hangupCall();
    }
    if (m_callKeyboardPanel) {
        m_callKeyboardPanel->hide();
    }
    if (m_tabWrap) {
        m_tabWrap->show();
    }
    if (m_tabStack) {
        m_tabStack->setCurrentIndex(m_previousTabIndex);
        m_tabStack->show();
    }
    if (m_numberEdit && m_tabStack && m_tabStack->currentIndex() == 0) {
        m_numberEdit->show();
    }
    if (m_callNumber) {
        addCallLogEntry(3, QStringLiteral("拨出"), m_callNumber->text());
    }
}

void PhoneWindow::onDialTab() {
    activateTab(0);
}

void PhoneWindow::onHistoryTab() {
    if (m_callEntries.isEmpty() && m_bluetoothManager) {
        m_historyList->clear();
        startCallLogSync();
    }
    activateTab(1);
}

void PhoneWindow::onContactsTab() {
    if (m_contactEntries.isEmpty() && m_bluetoothManager) {
        m_contactList->clear();
        startPhonebookSync();
    }
    activateTab(2);
}

void PhoneWindow::onBluetoothCallStatusChanged(int status) {
    if (!m_callStateLabel) {
        return;
    }
    switch (status) {
    case 1:
        if (m_tabWrap) {
            m_tabWrap->show();
        }
        if (m_tabStack) {
            m_tabStack->show();
        }
        activateTab(0);
        break;
    case 2:
        m_callStateLabel->setText(QStringLiteral("连接中..."));
        break;
    case 3:
        m_callStateLabel->setText(QStringLiteral("已连接"));
        break;
    case 4:
        showCallOverlay(4);
        break;
    case 5:
        showCallOverlay(5);
        break;
    case 6:
        showCallOverlay(6);
        break;
    default:
        break;
    }
}

void PhoneWindow::onBluetoothCallNumberUpdated(const QString &number, const QString &source) {
    if (!m_callNumber) {
        return;
    }
    if (!number.trimmed().isEmpty()) {
        m_callNumber->setText(number.trimmed());
    }
}

void PhoneWindow::onBluetoothPhonebookEntryReceived(const QString &name, const QString &number) {
    addContactEntry(name, number);
}

void PhoneWindow::onBluetoothCallLogEntryReceived(int type, const QString &name, const QString &number) {
    addCallLogEntry(type, name, number);
}

void PhoneWindow::onBluetoothPhonebookDownloadFinished() {
    if (m_bluetoothManager) {
        const QString address = m_bluetoothManager->getConnectedDeviceAddress().trimmed().toUpper();
        if (!address.isEmpty()) {
            m_lastSyncedDeviceAddress = address;
        }
    }
    if (m_bluetoothManager) {
        const QString address = m_bluetoothManager->getConnectedDeviceAddress().trimmed().toUpper();
        if (!address.isEmpty()) {
            m_lastSyncedDeviceAddress = address;
        }
    }
    if (m_contactList) {
        rebuildContactList();
    }
}

void PhoneWindow::onBluetoothCallLogDownloadFinished() {
    if (m_bluetoothManager) {
        const QString address = m_bluetoothManager->getConnectedDeviceAddress().trimmed().toUpper();
        if (!address.isEmpty()) {
            m_lastSyncedCallLogDeviceAddress = address;
        }
    }
    if (m_historyList) {
        rebuildHistoryList();
    }
}

void PhoneWindow::startPhonebookSync() {
    if (!m_bluetoothManager) {
        return;
    }
    m_contactEntries.clear();
    if (m_contactList) {
        m_contactList->clear();
    }
    m_bluetoothManager->requestPhonebookDownload();
}

void PhoneWindow::startCallLogSync() {
    if (!m_bluetoothManager) {
        return;
    }
    m_callEntries.clear();
    if (m_historyList) {
        m_historyList->clear();
    }
    m_bluetoothManager->requestCallLogDownload();
}

void PhoneWindow::onBluetoothDeviceConnected(const QString &name) {
    Q_UNUSED(name);
    if (!m_bluetoothManager) {
        return;
    }

    const QString connectedAddress = m_bluetoothManager->getConnectedDeviceAddress().trimmed().toUpper();
    const bool samePhonebookDevice = !connectedAddress.isEmpty() && connectedAddress == m_lastSyncedDeviceAddress;
    const bool sameCallLogDevice = !connectedAddress.isEmpty() && connectedAddress == m_lastSyncedCallLogDeviceAddress;

    const int currentTab = m_tabStack ? m_tabStack->currentIndex() : -1;
    if (currentTab == 2) {
        if (!samePhonebookDevice) {
            startPhonebookSync();
        }
    } else if (currentTab == 1) {
        if (!sameCallLogDevice) {
            startCallLogSync();
        }
    } else {
        if (!samePhonebookDevice) {
            startPhonebookSync();
        }
        if (!sameCallLogDevice) {
            startCallLogSync();
        }
    }
}

void PhoneWindow::populateHistoryList() {
    rebuildHistoryList();
}

void PhoneWindow::populateContactList() {
    rebuildContactList();
}

void PhoneWindow::rebuildHistoryList() {
    if (!m_historyList) {
        return;
    }
    m_historyList->clear();
    for (const CallLogEntry &entry : qAsConst(m_callEntries)) {
        const QString stateIcon = entry.type == 4 ? QStringLiteral(":/images/pict_callinglist_state_2.png")
                                : entry.type == 5 ? QStringLiteral(":/images/pict_callinglist_state_3.png")
                                : QStringLiteral(":/images/pict_callinglist_state_1.png");
        auto *item = new QListWidgetItem(m_historyList);
        item->setData(Qt::UserRole, entry.number);
        item->setSizeHint(QSize(0, 68));
        m_historyList->addItem(item);
        m_historyList->setItemWidget(item, createHistoryRow(entry.name, entry.number, entry.timeText, stateIcon, true));
    }
}

void PhoneWindow::rebuildContactList() {
    if (!m_contactList) {
        return;
    }
    m_contactList->setUpdatesEnabled(false);
    m_contactList->clear();
    for (const QPair<QString, QString> &contact : qAsConst(m_contactEntries)) {
        auto *item = new QListWidgetItem(m_contactList);
        item->setData(Qt::UserRole, contact.second);
        item->setSizeHint(QSize(0, 68));
        m_contactList->addItem(item);
        m_contactList->setItemWidget(item, createContactRow(contact.first, contact.second));
    }
    m_contactList->setUpdatesEnabled(true);
}

void PhoneWindow::rebuildDetailList(const QString &number) {
    if (!m_detailListLayout) {
        return;
    }
    while (QLayoutItem *item = m_detailListLayout->takeAt(0)) {
        if (QWidget *widget = item->widget()) {
            widget->deleteLater();
        }
        delete item;
    }

    bool hasEntries = false;
    for (const CallLogEntry &entry : qAsConst(m_callEntries)) {
        if (entry.number == number) {
            const QString stateIcon = entry.type == 4 ? QStringLiteral(":/images/pict_callinglist_state_2.png")
                                    : entry.type == 5 ? QStringLiteral(":/images/pict_callinglist_state_3.png")
                                    : QStringLiteral(":/images/pict_callinglist_state_1.png");
            m_detailListLayout->addWidget(createDetailLogRow(entry.timeText, QStringLiteral("00:00"), stateIcon));
            hasEntries = true;
        }
    }
    if (!hasEntries) {
        m_detailListLayout->addWidget(createDetailLogRow(QStringLiteral("暂无通话记录"), QStringLiteral(""), QStringLiteral(":/images/pict_callinglist_state_1.png")));
    }
    m_detailListLayout->addStretch();
}

int PhoneWindow::findContactEntryIndex(const QString &number) const {
    const QString trimmed = number.trimmed();
    for (int i = 0; i < m_contactEntries.size(); ++i) {
        if (m_contactEntries[i].second == trimmed) {
            return i;
        }
    }
    return -1;
}

void PhoneWindow::insertContactWidget(int index, const QString &name, const QString &number) {
    if (!m_contactList) {
        return;
    }
    auto *item = new QListWidgetItem(m_contactList);
    item->setData(Qt::UserRole, number);
    item->setSizeHint(QSize(0, 68));
    if (index >= 0 && index < m_contactList->count()) {
        m_contactList->insertItem(index, item);
    } else {
        m_contactList->addItem(item);
    }
    m_contactList->setItemWidget(item, createContactRow(name, number));
}

void PhoneWindow::updateContactWidget(int index, const QString &name, const QString &number) {
    if (!m_contactList || index < 0 || index >= m_contactList->count()) {
        return;
    }
    QListWidgetItem *item = m_contactList->item(index);
    if (!item) {
        return;
    }
    if (QWidget *oldWidget = m_contactList->itemWidget(item)) {
        oldWidget->deleteLater();
    }
    m_contactList->setItemWidget(item, createContactRow(name, number));
}

void PhoneWindow::addContactEntry(const QString &name, const QString &number) {
    const QString trimmed = number.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }
    QString finalName = name.trimmed();
    if (finalName.isEmpty()) {
        finalName = trimmed;
    }

    const int existingIndex = findContactEntryIndex(trimmed);
    if (existingIndex != -1) {
        if (m_contactEntries[existingIndex].first == finalName) {
            return;
        }
        m_contactEntries.removeAt(existingIndex);
        if (m_contactList) {
            QListWidgetItem *oldItem = m_contactList->takeItem(existingIndex);
            if (oldItem) {
                if (QWidget *oldWidget = m_contactList->itemWidget(oldItem)) {
                    oldWidget->deleteLater();
                }
                delete oldItem;
            }
        }
    }

    const auto insertPos = std::lower_bound(m_contactEntries.begin(), m_contactEntries.end(), qMakePair(finalName, trimmed), [](const QPair<QString, QString> &a, const QPair<QString, QString> &b) {
        return a.first.toLower() < b.first.toLower();
    });
    const int index = std::distance(m_contactEntries.begin(), insertPos);
    m_contactEntries.insert(insertPos, {finalName, trimmed});
    s_cachedContactEntries = m_contactEntries;
    if (m_contactList) {
        insertContactWidget(index, finalName, trimmed);
    }
}

void PhoneWindow::insertHistoryWidget(int index, const CallLogEntry &entry) {
    if (!m_historyList) {
        return;
    }

    const QString stateIcon = entry.type == 4 ? QStringLiteral(":/images/pict_callinglist_state_2.png")
                            : entry.type == 5 ? QStringLiteral(":/images/pict_callinglist_state_3.png")
                            : QStringLiteral(":/images/pict_callinglist_state_1.png");

    auto *item = new QListWidgetItem(m_historyList);
    item->setData(Qt::UserRole, entry.number);
    item->setSizeHint(QSize(0, 68));
    if (index >= 0 && index < m_historyList->count()) {
        m_historyList->insertItem(index, item);
    } else {
        m_historyList->addItem(item);
    }
    m_historyList->setItemWidget(item, createHistoryRow(entry.name, entry.number, entry.timeText, stateIcon, true));
}

void PhoneWindow::addCallLogEntry(int type, const QString &name, const QString &number) {
    const QString trimmed = number.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }
    QString now = QDateTime::currentDateTime().toString(QStringLiteral("yyyy.MM.dd  ") + AppSignals::timeFormat());
    m_callEntries.prepend({type, name.isEmpty() ? trimmed : name, trimmed, now});

    const bool trimmedOld = m_callEntries.size() > 50;
    if (trimmedOld) {
        m_callEntries.removeLast();
    }

    if (m_historyList) {
        insertHistoryWidget(0, m_callEntries.first());
        if (trimmedOld || m_historyList->count() > m_callEntries.size()) {
            QListWidgetItem *lastItem = m_historyList->takeItem(m_historyList->count() - 1);
            if (lastItem) {
                if (QWidget *oldWidget = m_historyList->itemWidget(lastItem)) {
                    oldWidget->deleteLater();
                }
                delete lastItem;
            }
        }
    }
}

QWidget *PhoneWindow::createHistoryRow(const QString &name, const QString &number, const QString &timeText, const QString &stateIcon, bool detailButton) {
    auto *row = new QWidget();
    row->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    row->setFixedHeight(68);
    row->setStyleSheet("QWidget{background:rgba(255,255,255,0.1);border-radius:34px;}");

    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(24, 4, 24, 4);
    layout->setSpacing(12);

    auto *userWrap = new QWidget(row);
    userWrap->setStyleSheet("background:transparent;");
    userWrap->setFixedWidth(320);
    auto *userLayout = new QHBoxLayout(userWrap);
    userLayout->setContentsMargins(0, 0, 0, 0);
    userLayout->setSpacing(16);
    auto *userIcon = new QLabel(userWrap);
    userIcon->setStyleSheet("background:transparent;");
    userIcon->setPixmap(QPixmap(":/images/pict_callinglist_user.png").scaled(52, 52, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    auto *userLabel = new QLabel(name, userWrap);
    userLabel->setStyleSheet("QLabel{color:#fff;font-size:32px;background:transparent;}");
    userLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    userLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    userLayout->addWidget(userIcon);
    userLayout->addWidget(userLabel);

    auto *stateIconLabel = new QLabel(row);
    stateIconLabel->setStyleSheet("background:transparent;");
    stateIconLabel->setPixmap(QPixmap(stateIcon).scaled(32, 32, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    stateIconLabel->setFixedSize(32, 32);
    stateIconLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    auto *numLabel = new QLabel(number, row);
    numLabel->setStyleSheet("QLabel{color:#fff;font-size:24px;background:transparent;}");
    numLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    numLabel->setFixedWidth(228);

    auto *timeLabel = new QLabel(timeText, row);
    timeLabel->setStyleSheet("QLabel{color:#fff;font-size:24px;background:transparent;}");
    timeLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    timeLabel->setFixedWidth(260);

    auto *detailBtn = new QPushButton(row);
    detailBtn->setFixedSize(60, 60);
    detailBtn->setCursor(Qt::PointingHandCursor);
    detailBtn->setStyleSheet(detailButton
                                ? "QPushButton{border:none;background:url(:/images/butt_callinglist_detail_up.png) no-repeat right center;}QPushButton:hover{background:url(:/images/butt_callinglist_detail_down.png) no-repeat right center;}"
                                : "QPushButton{border:none;background:transparent;}");
    connect(detailBtn, &QPushButton::clicked, this, [this, name, number]() {
        showContactDetail(name, number);
    });

    layout->addWidget(userWrap);
    layout->addWidget(stateIconLabel);
    layout->addWidget(numLabel);
    layout->addWidget(timeLabel);
    layout->addWidget(detailBtn);
    return row;
}

QWidget *PhoneWindow::createContactRow(const QString &name, const QString &number) {
    auto *row = new QWidget();
    row->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    row->setFixedHeight(68);
    row->setStyleSheet("QWidget{background:rgba(255,255,255,0.1);border-radius:34px;}");

    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(24, 4, 24, 4);
    layout->setSpacing(12);

    auto *userWrap = new QWidget(row);
    userWrap->setStyleSheet("background:transparent;");
    auto *userLayout = new QHBoxLayout(userWrap);
    userLayout->setContentsMargins(0, 0, 0, 0);
    userLayout->setSpacing(24);
    auto *userIcon = new QLabel(userWrap);
    userIcon->setStyleSheet("background:transparent;");
    userIcon->setPixmap(QPixmap(":/images/pict_callinglist_user.png").scaled(52, 52, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    auto *nameLabel = new QLabel(name, userWrap);
    nameLabel->setStyleSheet("QLabel{color:#fff;font-size:32px;background:transparent;}");
    userLayout->addWidget(userIcon);
    userLayout->addWidget(nameLabel);

    auto *numLabel = new QLabel(number, row);
    numLabel->setStyleSheet("QLabel{color:#fff;font-size:24px;background:transparent;}");
    numLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    numLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    auto *detailBtn = new QPushButton(row);
    detailBtn->setFixedSize(60, 60);
    detailBtn->setCursor(Qt::PointingHandCursor);
    detailBtn->setStyleSheet("QPushButton{border:none;background:url(:/images/butt_callinglist_detail_up.png) no-repeat right center;}"
                             "QPushButton:hover{background:url(:/images/butt_callinglist_detail_down.png) no-repeat right center;}");
    connect(detailBtn, &QPushButton::clicked, this, [this, name, number]() {
        showContactDetail(name, number);
    });

    auto *callBtn = new QPushButton(row);
    callBtn->setFixedSize(60, 60);
    callBtn->setCursor(Qt::PointingHandCursor);
    callBtn->setStyleSheet("QPushButton{border:none;background:url(:/images/butt_calllinglist_answer_up.png) no-repeat right center;}"
                           "QPushButton:hover{background:url(:/images/butt_calllinglist_answer_down.png) no-repeat right center;}");
    connect(callBtn, &QPushButton::clicked, this, [this, number]() {
        cacheDialNumber(number);
        if (m_bluetoothManager) {
            m_bluetoothManager->dialNumber(number);
        }
    });

    layout->addWidget(userWrap);
    layout->addStretch();
    numLabel->setFixedWidth(300);
    layout->addWidget(numLabel);
    layout->addWidget(detailBtn);
    layout->addWidget(callBtn);
    return row;
}

void PhoneWindow::showCallOverlay(int status) {
    if (m_tabStack && m_callOverlay && m_tabStack->currentWidget() != m_callOverlay) {
        m_previousTabIndex = m_tabStack->currentIndex();
    }
    if (m_numberEdit && m_callNumber) {
        const QString currentDial = m_numberEdit->text().trimmed();
        if (!currentDial.isEmpty()) {
            m_callNumber->setText(currentDial);
        }
    }
    updateCallPanel(status);
    if (m_callKeyboardPanel) {
        m_callKeyboardPanel->hide();
    }
    if (m_tabWrap) {
        m_tabWrap->hide();
    }
    if (m_tabStack && m_callOverlay) {
        m_tabStack->setCurrentWidget(m_callOverlay);
    }
    if (m_numberEdit) {
        m_numberEdit->hide();
    }
    if (m_bottomActions) {
        m_bottomActions->show();
    }
}

void PhoneWindow::updateCallPanel(int status) {
    if (!m_callStateLabel || !m_callTimer || !m_answerButton) {
        return;
    }

    switch (status) {
    case 4:
        m_callStateLabel->setText(QStringLiteral("呼出中..."));
        m_callTimer->setText(QStringLiteral("00:00"));
        m_callTimer->show();
        m_answerButton->hide();
        break;
    case 5:
        m_callStateLabel->setText(QStringLiteral("来电..."));
        m_callTimer->hide();
        m_answerButton->show();
        break;
    case 6:
        m_callStateLabel->setText(QStringLiteral("通话中..."));
        m_callTimer->setText(QStringLiteral("00:00"));
        m_callTimer->show();
        m_answerButton->hide();
        break;
    default:
        m_callStateLabel->setText(QStringLiteral("通话中..."));
        m_callTimer->setText(QStringLiteral("00:00"));
        m_callTimer->show();
        m_answerButton->hide();
        break;
    }
}

QWidget *PhoneWindow::createDetailLogRow(const QString &timeText, const QString &durationText, const QString &stateIcon) {
    auto *row = new QWidget();
    row->setFixedSize(872, 68);
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
    if (m_tabStack) {
        m_previousTabIndex = m_tabStack->currentIndex();
    }
    if (m_detailNameLabel) {
        m_detailNameLabel->setText(name);
    }
    if (m_detailNumberLabel) {
        m_detailNumberLabel->setText(number);
    }
    rebuildDetailList(number);
    if (m_tabWrap) {
        m_tabWrap->hide();
    }
    if (m_tabStack) {
        m_tabStack->hide();
    }
    if (m_numberEdit) {
        m_numberEdit->hide();
    }
    if (m_detailOverlay) {
        m_detailOverlay->show();
        if (m_topBar) {
            m_detailOverlay->stackUnder(m_topBar);
        }
    }
    if (m_topBar) {
        m_topBar->show();
        m_topBar->raise();
    }
}

void PhoneWindow::hideContactDetail() {
    if (m_detailOverlay) {
        m_detailOverlay->hide();
    }
    if (m_tabWrap) {
        m_tabWrap->show();
    }
    if (m_tabStack) {
        activateTab(m_previousTabIndex >= 0 && m_previousTabIndex <= 2 ? m_previousTabIndex : 1);
        m_tabStack->show();
    }
    if (m_numberEdit && m_tabStack && m_tabStack->currentIndex() == 0) {
        m_numberEdit->show();
    }
}

void PhoneWindow::keyPressEvent(QKeyEvent *event)
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
