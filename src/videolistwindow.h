#ifndef VIDEOLISTWINDOW_H
#define VIDEOLISTWINDOW_H

#include <QMainWindow>
#include <QListWidget>
#include <QLabel>
#include <QPushButton>
#include <QStyledItemDelegate>
#include <QPainter>

class VideoPlayWindow;

// 自定义委托来绘制item背景（匹配HTML设计）
class VideoListItemDelegate : public QStyledItemDelegate {
public:
    explicit VideoListItemDelegate(QObject *parent = nullptr) : QStyledItemDelegate(parent) {}
    
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        Q_UNUSED(option);
        Q_UNUSED(index);
        return QSize(160, 160);  // 与HTML li尺寸一致
    }
    
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        painter->save();
        
        // 绘制160x160背景图像
        bool isDirectory = index.data(Qt::UserRole + 1).toBool();
        QPixmap pixmap;
        if (isDirectory) {
            pixmap.load(":/images/music_musiclist_file_up.png");
        } else {
            pixmap.load(":/images/music_musiclist_up.png");
        }
        
        // 在gridCell中居左上角绘制160x160的图
        QRect itemRect(option.rect.x(), option.rect.y(), 160, 160);
        if (!pixmap.isNull()) {
            painter->drawPixmap(itemRect, pixmap);
        }
        
        // 绘制文本（padding-top: 117px, line-height: 42px, 居中）
        QString text = index.data(Qt::DisplayRole).toString();
        QRect textRect(itemRect.x(), itemRect.y() + 117, 160, 42);
        
        painter->setPen(QPen(Qt::white));
        QFont font;
        font.setPixelSize(20);  // font-size: 20px
        painter->setFont(font);
        painter->drawText(textRect, Qt::AlignHCenter | Qt::AlignVCenter, text);
        
        painter->restore();
    }
};

class VideoListWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit VideoListWindow(QWidget *parent = nullptr);
    ~VideoListWindow();

signals:
    void requestReturnToMain();

private slots:
    void onHomeClicked();
    void onBackClicked();
    void onItemClicked(QListWidgetItem *item);

private:
    void setupUI();
    void loadVideoFiles(const QString &directory = "");
    void updatePath(const QString &path);

protected:
    void closeEvent(QCloseEvent *event) override;

    QPushButton *m_homeButton;
    QPushButton *m_backDirButton;
    QListWidget *m_videoListWidget;
    QLabel *m_pathLabel;
    QLabel *m_timeLabel;
    
    QString m_currentPath;
    QString m_initialPath;
    QStringList m_videoExtensions;

    VideoPlayWindow *m_playWindow = nullptr;
};

#endif // VIDEOLISTWINDOW_H
