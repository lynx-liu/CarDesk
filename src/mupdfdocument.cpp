#include "mupdfdocument.h"

#include <QByteArray>
#include <QFileInfo>

#include <mupdf/fitz.h>
#include <mupdf/fitz/util.h>

MuPdfDocument::MuPdfDocument()
    : m_ctx(nullptr)
    , m_doc(nullptr)
{
}

MuPdfDocument::~MuPdfDocument()
{
    close();
}

bool MuPdfDocument::ensureContext()
{
    if (m_ctx) {
        return true;
    }

    m_ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
    if (!m_ctx) {
        m_lastError = QStringLiteral("无法初始化 MuPDF 上下文");
        return false;
    }

    fz_try(m_ctx) {
        fz_register_document_handlers(m_ctx);
    }
    fz_catch(m_ctx) {
        m_lastError = QString::fromLatin1(fz_caught_message(m_ctx));
        fz_drop_context(m_ctx);
        m_ctx = nullptr;
        return false;
    }

    return true;
}

bool MuPdfDocument::openFile(const QString &filePath)
{
    close();

    if (!ensureContext()) {
        return false;
    }

    const QByteArray rawPath = QFile::encodeName(filePath);

    fz_try(m_ctx) {
        m_doc = fz_open_document(m_ctx, rawPath.constData());
    }
    fz_catch(m_ctx) {
        m_lastError = QString::fromLatin1(fz_caught_message(m_ctx));
        return false;
    }

    return m_doc != nullptr;
}

void MuPdfDocument::close()
{
    if (m_doc && m_ctx) {
        fz_drop_document(m_ctx, m_doc);
        m_doc = nullptr;
    }

    if (m_ctx) {
        fz_drop_context(m_ctx);
        m_ctx = nullptr;
    }
}

bool MuPdfDocument::isOpen() const
{
    return m_doc != nullptr;
}

int MuPdfDocument::pageCount() const
{
    if (!m_doc || !m_ctx) {
        return 0;
    }
    return fz_count_pages(m_ctx, m_doc);
}

QImage MuPdfDocument::renderPage(int pageIndex, int maxWidth, int maxHeight, QString *error)
{
    if (!m_doc || !m_ctx) {
        return QImage();
    }

    if (pageIndex < 0 || pageIndex >= pageCount()) {
        return QImage();
    }

    fz_page *page = nullptr;
    fz_pixmap *pixmap = nullptr;
    QImage result;

    fz_try(m_ctx) {
        page = fz_load_page(m_ctx, m_doc, pageIndex);
        fz_rect bounds = fz_bound_page(m_ctx, page);

        const float pageWidth = bounds.x1 - bounds.x0;
        const float pageHeight = bounds.y1 - bounds.y0;
        float scale = 1.0f;
        if (maxWidth > 0 && maxHeight > 0 && pageWidth > 0.0f && pageHeight > 0.0f) {
            scale = qMin(maxWidth / pageWidth, maxHeight / pageHeight);
            if (scale <= 0.0f) {
                scale = 1.0f;
            }
        }

        const fz_matrix ctm = fz_scale(scale, scale);
        pixmap = fz_new_pixmap_from_page(m_ctx, page, ctm, fz_device_rgb(m_ctx), 0);
        fz_drop_page(m_ctx, page);
        page = nullptr;

        const int width = fz_pixmap_width(m_ctx, pixmap);
        const int height = fz_pixmap_height(m_ctx, pixmap);
        const int stride = fz_pixmap_stride(m_ctx, pixmap);
        const int components = fz_pixmap_components(m_ctx, pixmap);
        const unsigned char *samples = fz_pixmap_samples(m_ctx, pixmap);

        if (width > 0 && height > 0 && samples) {
            if (components == 3) {
                result = QImage(samples, width, height, stride, QImage::Format_RGB888).copy();
            } else if (components == 4) {
                result = QImage(samples, width, height, stride, QImage::Format_RGBA8888).copy();
            } else {
                result = QImage(samples, width, height, stride, QImage::Format_RGB888).copy();
            }
        }

        if (pixmap) {
            fz_drop_pixmap(m_ctx, pixmap);
            pixmap = nullptr;
        }
    }
    fz_catch(m_ctx) {
        m_lastError = QString::fromLatin1(fz_caught_message(m_ctx));
        if (page) {
            fz_drop_page(m_ctx, page);
            page = nullptr;
        }
        if (pixmap) {
            fz_drop_pixmap(m_ctx, pixmap);
            pixmap = nullptr;
        }
    }

    if (error) {
        *error = m_lastError;
    }
    return result;
}

QString MuPdfDocument::lastError() const
{
    return m_lastError;
}
