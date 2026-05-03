#ifndef IMAGEVIEWINGWINDOW_H
#define IMAGEVIEWINGWINDOW_H

#include <QMainWindow>
#include <QStringList>
#include <QStyledItemDelegate>
#include <QPainter>
#include <QGestureEvent>
#include <QThread>
#include <QImage>

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
        QRect cell(option.rect.x(), option.rect.y(), 160, 160);

        if (isDir) {
            QPixmap pix;
            pix.load(QStringLiteral(":/images/butt_driving_image_playback_folder_up.png"));
            if (!pix.isNull()) painter->drawPixmap(cell, pix);
        } else {
            const QPixmap thumb = index.data(Qt::UserRole + 2).value<QPixmap>();
            if (!thumb.isNull()) {
                // Draw thumbnail in top area, dark strip behind text
                QRect thumbRect(cell.x(), cell.y(), 160, 117);
                painter->drawPixmap(thumbRect, thumb);
                painter->fillRect(QRect(cell.x(), cell.y() + 117, 160, 43),
                                  QColor(0, 0, 0, 180));
            } else {
                QPixmap pix;
                pix.load(QStringLiteral(":/images/image_imagellist_up.png"));
                if (!pix.isNull()) painter->drawPixmap(cell, pix);
            }
        }

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

// Async thumbnail worker — lives in a dedicated QThread
class ThumbnailLoader : public QObject {
    Q_OBJECT
public:
    explicit ThumbnailLoader(QObject *parent = nullptr)
        : QObject(parent), m_cancel(false) {}

    void cancel() { m_cancel = true; }

public slots:
    void process(QStringList paths) {
        m_cancel = false;
        for (const QString &p : paths) {
            if (m_cancel) break;
            QImage img(p);
            QImage thumb(160, 117, QImage::Format_RGB32);
            thumb.fill(Qt::black);
            if (!img.isNull()) {
                const QImage scaled = img.scaled(
                    160, 117, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                QPainter painter(&thumb);
                painter.drawImage(
                    (160 - scaled.width())  / 2,
                    (117 - scaled.height()) / 2,
                    scaled);
            }
            if (!m_cancel)
                emit thumbnailReady(p, thumb);
        }
    }

signals:
    void thumbnailReady(QString path, QImage image);

private:
    volatile bool m_cancel;
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
    void showEvent(QShowEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;

private slots:
    void onPrevImage();
    void onNextImage();
    void onOpenCurrentImage();
    void onBackToList();
    void onRotateImage();
    void onItemClicked(QListWidgetItem *item);
    void onBackDirClicked();
    void onThumbnailReady(const QString &path, const QImage &image);

private:
    void setupUI();
    void loadDirectory(const QString &path);
    void updateImageView();

    QStackedWidget *m_modeStack;
    QWidget       *m_viewPage;
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
    double m_zoomFactor;
    bool m_isPinching;

    ThumbnailLoader *m_thumbLoader;
    QThread         *m_loaderThread;

    // cached source for fast zoom (no re-read from disk per gesture frame)
    QPixmap m_cachedSourcePixmap;
    QString m_cachedImagePath;
    int     m_cachedRotation;

    QString m_currentPath;
    QString m_initialPath;
    QStringList m_imageFiles;
    QStringList m_imageExtensions;
};

#endif // IMAGEVIEWINGWINDOW_H
