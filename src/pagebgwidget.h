#pragma once

#include <QPainter>
#include <QPixmap>
#include <QWidget>

// 共享背景 Widget。
// background.png 在整个进程生命周期内仅解码一次（static QPixmap），
// 所有继承或使用本类的页面共享同一份像素数据，消除各窗口独立加载/CSS解析开销。
// 不含 Q_OBJECT，无需 moc 处理。
class PageBgWidget : public QWidget
{
public:
    explicit PageBgWidget(QWidget *parent = nullptr) : QWidget(parent) {}

    // 在 QApplication 启动后（资源系统就绪）调用一次，提前解码背景图到内存。
    static void prewarm() { (void)bgPixmap(); }

protected:
    void paintEvent(QPaintEvent *event) override {
        QPainter p(this);
        p.drawPixmap(rect(), bgPixmap());
        QWidget::paintEvent(event);
    }

private:
    static const QPixmap &bgPixmap() {
        static const QPixmap s(QStringLiteral(":/images/background.png"));
        return s;
    }
};
