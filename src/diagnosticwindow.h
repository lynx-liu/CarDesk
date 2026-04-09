#ifndef DIAGNOSTICWINDOW_H
#define DIAGNOSTICWINDOW_H

#include <QMainWindow>
#include <QMap>
#include <QVector>
#include "mcuserialreader.h"

class QLabel;
class QLineEdit;
class QScrollArea;
class QStackedWidget;
class QWidget;

class DiagnosticWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit DiagnosticWindow(QWidget *parent = nullptr);

signals:
    void requestReturnToMain();

protected:
    void closeEvent(QCloseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private slots:
    void onOpenFaultPage();
    void onOpenFaultDetailPage();
    void onOpenMaintenanceBookPage();
    void onOpenPdfView();
    void onOpenPdfSearchPage();
    void onOpenPdfJumpPage();
    void onConfirmPdfSearch();
    void onPrevSearchResult();
    void onNextSearchResult();
    void onConfirmPdfJump();
    void onPrevPage();
    void onNextPage();
    void onFaultDataReceived(const QString &controller, const QVector<McuFaultInfo> &faults);

private:
    void setupUI();
    QWidget *createMainMenuPage();
    QWidget *createFaultPage();
    QWidget *createFaultDetailPage();
    QWidget *createMaintenanceBookPage();
    QWidget *createPdfPage();
    QWidget *createPdfSearchPage();
    QWidget *createPdfJumpPage();
    void openPage(int index);
    void appendCharToInput(QLineEdit *target, const QString &text);
    void updatePdfHeader();
    void updateSearchResultHeader();
    void showFaultDetail(const QString &controller);
    void populateFaultDetailContent();
    static int controllerIndex(const QString &ctrl);

    QStackedWidget *m_pages;

    QLabel *m_pdfHeaderLabel;
    QLabel *m_pdfSearchKeywordLabel;
    QLabel *m_pdfSearchResultLabel;
    QLineEdit *m_searchInput;
    QLineEdit *m_jumpInput;
    QWidget *m_pdfBottomNormal;
    QWidget *m_pdfBottomSearch;
    int m_pdfPage;
    int m_pdfTotal;
    int m_resultIndex;
    int m_resultTotal;

    // 故障诊断实时数据
    McuSerialReader                      *m_reader;
    QMap<QString, QVector<McuFaultInfo>>  m_activeFaults;
    QLabel                               *m_faultBadgeLabels[3];
    QString                               m_currentFaultController;
    QLabel                               *m_faultDetailTitleLabel;
    QScrollArea                          *m_faultDetailScrollArea;
};

#endif // DIAGNOSTICWINDOW_H
