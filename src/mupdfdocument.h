#ifndef MUPDFDOCUMENT_H
#define MUPDFDOCUMENT_H

#include <QImage>
#include <QString>

struct fz_context;
struct fz_document;

class MuPdfDocument
{
public:
    MuPdfDocument();
    ~MuPdfDocument();

    bool openFile(const QString &filePath);
    void close();
    bool isOpen() const;
    int pageCount() const;
    QImage renderPage(int pageIndex, int maxWidth, int maxHeight, QString *error = nullptr);
    QString lastError() const;

private:
    bool ensureContext();

    fz_context *m_ctx;
    fz_document *m_doc;
    QString m_lastError;
};

#endif // MUPDFDOCUMENT_H
