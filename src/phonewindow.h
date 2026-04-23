#ifndef PHONEWINDOW_H
#define PHONEWINDOW_H

#include <QList>
#include <QMainWindow>
#include <QString>

class QLineEdit;
class QListWidget;
class QPushButton;
class QLabel;
class QStackedWidget;
class QVBoxLayout;
class QWidget;

class BluetoothManager;

class PhoneWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit PhoneWindow(BluetoothManager *bluetoothManager, QWidget *parent = nullptr);

signals:
    void requestReturnToMain();

protected:
    void closeEvent(QCloseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private slots:
    void onDial();
    void onAnswer();
    void onHangup();
    void onDialTab();
    void onHistoryTab();
    void onContactsTab();
    void onBluetoothCallStatusChanged(int status);
    void onBluetoothCallNumberUpdated(const QString &number, const QString &source);
    void onBluetoothPhonebookEntryReceived(const QString &name, const QString &number);
    void onBluetoothCallLogEntryReceived(int type, const QString &name, const QString &number);
    void onBluetoothPhonebookDownloadFinished();
    void onBluetoothCallLogDownloadFinished();

private:
    void setupUI();
    void appendDigit(const QString &text);
    void cacheDialNumber(const QString &number);
    void activateTab(int index);
    void populateHistoryList();
    void populateContactList();
    void rebuildHistoryList();
    void rebuildContactList();
    void rebuildDetailList(const QString &number);
    void addCallLogEntry(int type, const QString &name, const QString &number);
    void addContactEntry(const QString &name, const QString &number);
    QWidget *createHistoryRow(const QString &name, const QString &number, const QString &timeText, const QString &stateIcon, bool detailButton);
    QWidget *createContactRow(const QString &name, const QString &number);
    QWidget *createDetailLogRow(const QString &timeText, const QString &durationText, const QString &stateIcon);
    void showCallOverlay(bool incoming);
    void updateCallPanel(bool incoming);
    void showContactDetail(const QString &name, const QString &number);
    void hideContactDetail();

    struct CallLogEntry {
        int type;
        QString name;
        QString number;
        QString timeText;
    };

    BluetoothManager *m_bluetoothManager;
    QWidget *m_topBar;
    QWidget *m_tabWrap;
    QStackedWidget *m_tabStack;
    QPushButton *m_tabDial;
    int m_previousTabIndex;
    QPushButton *m_tabHistory;
    QPushButton *m_tabContacts;
    static QString s_cachedDialNumber;

    QList<QPair<QString, QString>> m_contactEntries;
    QList<CallLogEntry> m_callEntries;

    QVBoxLayout *m_detailListLayout;
    QString m_detailCurrentNumber;

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
    QWidget *m_bottomActions;
    QPushButton *m_answerButton;
};

#endif // PHONEWINDOW_H
