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
class MediaManager;

class PhoneWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit PhoneWindow(BluetoothManager *bluetoothManager, MediaManager *mediaManager, QWidget *parent = nullptr);
    bool handlePhoneKeyPress();
    bool handleEndKeyPress();

signals:
    void requestReturnToMain();

protected:
    void closeEvent(QCloseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private slots:
    void onDial();
    void onAnswer();
    void onHangup();
    void onToggleMute();
    void onDialTab();
    void onHistoryTab();
    void onContactsTab();
    void onBluetoothCallStatusChanged(int status);
    void onBluetoothCallNumberUpdated(const QString &number, const QString &source);
    void onBluetoothPhonebookEntryReceived(const QString &name, const QString &number);
    void onBluetoothCallLogEntryReceived(int type, const QString &name, const QString &number, const QString &timeText);
    void onBluetoothPhonebookDownloadFinished();
    void onBluetoothCallLogDownloadFinished();
    void onBluetoothDeviceConnected(const QString &name);

private:
    void setupUI();
    void appendDigit(const QString &text);
    void cacheDialNumber(const QString &number);
    void activateTab(int index);
    void startPhonebookSync();
    void startCallLogSync();
    void insertContactWidget(int index, const QString &name, const QString &number);
    void updateContactWidget(int index, const QString &name, const QString &number);
    QWidget *createHistoryRow(const QString &name, const QString &number, const QString &timeText, const QString &stateIcon);
    QWidget *createContactRow(const QString &name, const QString &number);
    void showCallOverlay(int status);
    void updateCallPanel(int status);

    struct CallLogEntry {
        int type;
        QString name;
        QString number;
        QString timeText;
    };

    void populateHistoryList();
    void populateContactList();
    void rebuildHistoryList();
    void rebuildContactList();
    void insertHistoryWidget(int index, const CallLogEntry &entry);
    void addCallLogEntry(int type, const QString &name, const QString &number, const QString &timeText = QString());
    void addContactEntry(const QString &name, const QString &number);
    int findContactEntryIndex(const QString &number) const;
    QString findContactNameForNumber(const QString &number) const;

    BluetoothManager *m_bluetoothManager;
    MediaManager *m_mediaManager;
    QWidget *m_topBar;
    QWidget *m_tabWrap;
    QStackedWidget *m_tabStack;
    QPushButton *m_tabDial;
    int m_previousTabIndex;
    QPushButton *m_tabHistory;
    QPushButton *m_tabContacts;
    static QString s_cachedDialNumber;
    static QList<QPair<QString, QString>> s_cachedContactEntries;

    QList<QPair<QString, QString>> m_contactEntries;
    QList<CallLogEntry> m_callEntries;
    QString m_lastSyncedDeviceAddress;
    QString m_lastSyncedCallLogDeviceAddress;

    QLineEdit *m_numberEdit;
    QLabel *m_callNumber;
    QLabel *m_callTimer;
    QLabel *m_callStateLabel;
    QListWidget *m_historyList;
    QListWidget *m_contactList;
    QWidget *m_callOverlay;
    QWidget *m_callKeyboardPanel;
    QWidget *m_bottomActions;
    QPushButton *m_answerButton;
    QPushButton *m_muteButton;
    bool m_callMuted;
    int m_currentCallStatus;
};

#endif // PHONEWINDOW_H
