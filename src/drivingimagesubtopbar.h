#ifndef DRIVINGIMAGESUBTOPBAR_H
#define DRIVINGIMAGESUBTOPBAR_H

#include <QWidget>

class QLabel;
class QPushButton;
class TopBarRightWidget;

class DrivingImageSubTopBar : public QWidget {
    Q_OBJECT

public:
    explicit DrivingImageSubTopBar(QWidget *parent = nullptr);

    void setTitle(const QString &title);

signals:
    void homeClicked();

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void layoutChildren();

    QPushButton *m_homeBtn = nullptr;
    QLabel *m_titleLabel = nullptr;
    TopBarRightWidget *m_topBarRight = nullptr;
};

#endif // DRIVINGIMAGESUBTOPBAR_H
