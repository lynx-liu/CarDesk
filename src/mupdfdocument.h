#ifndef MUPDFDOCUMENT_H
#define MUPDFDOCUMENT_H

#include <QImage>
#include <QRectF>
#include <QVector>
#include <QString>

struct fz_context;
struct fz_document;

class MuPdfDocument
{
public:
    struct PdfSearchHit {
        int pageIndex;
        QRectF bbox;
    };

    MuPdfDocument();
    ~MuPdfDocument();

    bool openFile(const QString &filePath);
    void close();
    bool isOpen() const;
    int pageCount() const;
    QImage renderPage(int pageIndex, int maxWidth, int maxHeight, QString *error = nullptr);
    QRectF pageBounds(int pageIndex) const;
    QRectF mapPdfRectToImage(int pageIndex, const QRectF &pdfRect, int imageWidth, int imageHeight, QString *error = nullptr) const;
    QVector<PdfSearchHit> searchDocument(const QString &keyword, QString *error = nullptr);
    QString lastError() const;

private:
    bool ensureContext();

    fz_context *m_ctx;
    fz_document *m_doc;
    QString m_lastError;
};

#endif // MUPDFDOCUMENT_H
