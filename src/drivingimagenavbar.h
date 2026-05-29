#ifndef DRIVINGIMAGENAVBAR_H
#define DRIVINGIMAGENAVBAR_H

#include <QWidget>

class QPushButton;

// 底部三按钮：实时预览 / 设置 / 回放（对齐 driving_image_bottom）
class DrivingImageNavBar : public QWidget {
    Q_OBJECT

public:
    enum Tab { TabPreview = 0, TabSettings = 1, TabPlayback = 2 };

    explicit DrivingImageNavBar(QWidget *parent = nullptr);

    void setActiveTab(Tab tab);

signals:
    void tabSelected(DrivingImageNavBar::Tab tab);

private:
    QPushButton *m_btnPreview = nullptr;
    QPushButton *m_btnSettings = nullptr;
    QPushButton *m_btnPlayback = nullptr;
};

#endif // DRIVINGIMAGENAVBAR_H
