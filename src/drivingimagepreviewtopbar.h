#ifndef DRIVINGIMAGEPREVIEWTOPBAR_H
#define DRIVINGIMAGEPREVIEWTOPBAR_H

#include <QWidget>

class QLabel;
class QPushButton;

// 预览长按呼出：对齐 UI .video_play_top（半透明条 + 视频返回键 + 标题）
class DrivingImagePreviewTopBar : public QWidget {
    Q_OBJECT

public:
    explicit DrivingImagePreviewTopBar(QWidget *parent = nullptr);

    void setTitle(const QString &title);

signals:
    void backClicked();

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void layoutChildren();

    QPushButton *m_backBtn = nullptr;
    QLabel *m_titleLabel = nullptr;
};

#endif // DRIVINGIMAGEPREVIEWTOPBAR_H
