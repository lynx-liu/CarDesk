#ifndef IMAGEVIEWINGWINDOW_H
#define IMAGEVIEWINGWINDOW_H

#include <QMainWindow>

class QLabel;
class QListWidget;
class QPushButton;
class QStackedWidget;

class ImageViewingWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit ImageViewingWindow(QWidget *parent = nullptr);

signals:
    void requestReturnToMain();

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onPrevImage();
    void onNextImage();
    void onThumbnailChanged(int row);
    void onOpenCurrentImage();
    void onBackToList();
    void onRotateImage();

private:
    void setupUI();
    void updateImageView();

    QStackedWidget *m_modeStack;
    QLabel *m_titleLabel;
    QLabel *m_viewTitleLabel;
    QLabel *m_previewLabel;
    QLabel *m_detailLabel;
    QListWidget *m_thumbnailList;
    QPushButton *m_prevButton;
    QPushButton *m_nextButton;
    QPushButton *m_rotateButton;
    int m_currentIndex;
    int m_rotationAngle;
};

#endif // IMAGEVIEWINGWINDOW_H
