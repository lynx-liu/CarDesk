#ifndef IMAGEVIEWINGWINDOW_H
#define IMAGEVIEWINGWINDOW_H

#include <QMainWindow>
#include <QStringList>
#include <QStyledItemDelegate>
#include <QPainter>

class ImageListItemDelegate : public QStyledItemDelegate {
public:
    explicit ImageListItemDelegate(QObject *parent = nullptr) : QStyledItemDelegate(parent) {}

    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override {
        return QSize(188, 178);
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override {
        painter->save();
        const bool isDir = index.data(Qt::UserRole + 1).toBool();
        QPixmap pix;
        if (isDir) {
            pix.load(QStringLiteral(":/images/butt_driving_image_playback_folder_up.png"));
        } else {
            pix.load(QStringLiteral(":/images/image_imagellist_up.png"));
        }
        QRect cell(option.rect.x(), option.rect.y(), 160, 160);
        if (!pix.isNull())
            painter->drawPixmap(cell, pix);
        const QString text = index.data(Qt::DisplayRole).toString();
        QRect textRect(cell.x(), cell.y() + 117, 160, 42);
        painter->setPen(Qt::white);
        QFont font;
        font.setPixelSize(20);
        painter->setFont(font);
        painter->drawText(textRect, Qt::AlignHCenter | Qt::AlignVCenter, text);
        painter->restore();
    }
};
class QLabel;
class QListWidget;
class QListWidgetItem;
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
    void keyPressEvent(QKeyEvent *event) override;

private slots:
    void onPrevImage();
    void onNextImage();
    void onOpenCurrentImage();
    void onBackToList();
    void onRotateImage();
    void onItemClicked(QListWidgetItem *item);
    void onBackDirClicked();

private:
    void setupUI();
    void loadDirectory(const QString &path);
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

    QString m_currentPath;
    QString m_initialPath;
    QStringList m_imageFiles;
    QStringList m_imageExtensions;
};

#endif // IMAGEVIEWINGWINDOW_H
