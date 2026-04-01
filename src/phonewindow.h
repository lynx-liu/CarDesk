#ifndef PHONEWINDOW_H
#define PHONEWINDOW_H

#include <QMainWindow>

class QLineEdit;
class QListWidget;
class QPushButton;
class QLabel;
class QStackedWidget;
class QWidget;

class PhoneWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit PhoneWindow(QWidget *parent = nullptr);

signals:
    void requestReturnToMain();

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onDial();
    void onHangup();
    void onDialTab();
    void onHistoryTab();
    void onContactsTab();

private:
    void setupUI();
    void appendDigit(const QString &text);
    void activateTab(int index);
    void populateHistoryList();
    void populateContactList();
    QWidget *createHistoryRow(const QString &name, const QString &number, const QString &timeText, const QString &stateIcon, bool detailButton);
    QWidget *createContactRow(const QString &name, const QString &number);
    QWidget *createDetailLogRow(const QString &timeText, const QString &durationText, const QString &stateIcon);
    void showCallOverlay(bool incoming);
    void updateCallPanel(bool incoming);
    void showContactDetail(const QString &name, const QString &number);
    void hideContactDetail();

    QStackedWidget *m_tabStack;
    QPushButton *m_tabDial;
    QPushButton *m_tabHistory;
    QPushButton *m_tabContacts;

    QLineEdit *m_numberEdit;
    QLabel *m_callNumber;
    QLabel *m_callTimer;
    QLabel *m_callStateLabel;
    QListWidget *m_historyList;
    QListWidget *m_contactList;
    QWidget *m_detailOverlay;
    QLabel *m_detailNameLabel;
    QLabel *m_detailNumberLabel;
    QWidget *m_callOverlay;
    QWidget *m_callKeyboardPanel;
    QPushButton *m_answerButton;
};

#endif // PHONEWINDOW_H
