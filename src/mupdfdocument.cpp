#include "mupdfdocument.h"

#include <QByteArray>
#include <QFileInfo>

#include <mupdf/fitz.h>
#include <mupdf/fitz/util.h>
#include <mupdf/fitz/geometry.h>

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

        const fz_matrix ctm = fz_transform_page(bounds, scale * 72.0f, 0);
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

QRectF MuPdfDocument::pageBounds(int pageIndex) const
{
    if (!m_doc || !m_ctx) {
        return QRectF();
    }
    if (pageIndex < 0 || pageIndex >= pageCount()) {
        return QRectF();
    }

    QRectF boundsRect;
    fz_page *page = nullptr;
    fz_try(m_ctx) {
        page = fz_load_page(m_ctx, m_doc, pageIndex);
        fz_rect bounds = fz_bound_page(m_ctx, page);
        boundsRect = QRectF(bounds.x0, bounds.y0, bounds.x1 - bounds.x0, bounds.y1 - bounds.y0);
        fz_drop_page(m_ctx, page);
        page = nullptr;
    }
    fz_catch(m_ctx) {
        if (page) {
            fz_drop_page(m_ctx, page);
            page = nullptr;
        }
    }
    return boundsRect;
}

QRectF MuPdfDocument::mapPdfRectToImage(int pageIndex, const QRectF &pdfRect, int imageWidth, int imageHeight, QString *error) const
{
    QRectF mappedRect;
    if (!m_doc || !m_ctx || pageIndex < 0 || pageIndex >= pageCount() || imageWidth <= 0 || imageHeight <= 0) {
        return mappedRect;
    }

    fz_page *page = nullptr;
    fz_try(m_ctx) {
        page = fz_load_page(m_ctx, m_doc, pageIndex);
        fz_rect bounds = fz_bound_page(m_ctx, page);
        const float pageWidth = bounds.x1 - bounds.x0;
        const float pageHeight = bounds.y1 - bounds.y0;
        float scale = 1.0f;
        if (pageWidth > 0 && pageHeight > 0) {
            scale = qMin(imageWidth / pageWidth, imageHeight / pageHeight);
            if (scale <= 0.0f) {
                scale = 1.0f;
            }
        }

        const fz_matrix ctm = fz_transform_page(bounds, scale * 72.0f, 0);
        fz_quad quad;
        const QRectF rect = pdfRect.normalized();
        quad.ul = { static_cast<float>(rect.left()), static_cast<float>(rect.top()) };
        quad.ur = { static_cast<float>(rect.right()), static_cast<float>(rect.top()) };
        quad.ll = { static_cast<float>(rect.left()), static_cast<float>(rect.bottom()) };
        quad.lr = { static_cast<float>(rect.right()), static_cast<float>(rect.bottom()) };
        quad = fz_transform_quad(quad, ctm);

        mappedRect = QRectF(quad.ul.x, quad.ul.y, 0, 0);
        mappedRect = mappedRect.united(QRectF(quad.ur.x, quad.ur.y, 0, 0));
        mappedRect = mappedRect.united(QRectF(quad.ll.x, quad.ll.y, 0, 0));
        mappedRect = mappedRect.united(QRectF(quad.lr.x, quad.lr.y, 0, 0));
        fz_drop_page(m_ctx, page);
        page = nullptr;
    }
    fz_catch(m_ctx) {
        const QString localError = QString::fromLatin1(fz_caught_message(m_ctx));
        if (page) {
            fz_drop_page(m_ctx, page);
            page = nullptr;
        }
        if (error) {
            *error = localError;
        }
        return QRectF();
    }

    if (error) {
        *error = QString();
    }
    return mappedRect;
}

struct MuPdfDocumentSearchCollector {
    QVector<MuPdfDocument::PdfSearchHit> *results;
    int pageIndex;
};

int MuPdfDocument_collect_search_hits(fz_context *ctx, void *opaque, int numQuads, fz_quad *hitBBox, int chapter, int page)
{
    Q_UNUSED(ctx);
    Q_UNUSED(chapter);
    if (!opaque || numQuads <= 0 || !hitBBox) {
        return 0;
    }

    auto *collector = static_cast<MuPdfDocumentSearchCollector *>(opaque);
    auto *results = collector->results;
    if (!results) {
        return 0;
    }

    float minX = hitBBox[0].ul.x;
    float minY = hitBBox[0].ul.y;
    float maxX = hitBBox[0].ul.x;
    float maxY = hitBBox[0].ul.y;
    for (int i = 0; i < numQuads; ++i) {
        const fz_quad &quad = hitBBox[i];
        const fz_point points[4] = {quad.ul, quad.ur, quad.ll, quad.lr};
        for (int j = 0; j < 4; ++j) {
            minX = qMin(minX, points[j].x);
            minY = qMin(minY, points[j].y);
            maxX = qMax(maxX, points[j].x);
            maxY = qMax(maxY, points[j].y);
        }
    }

    results->append({collector->pageIndex, QRectF(minX, minY, maxX - minX, maxY - minY)});
    return 0;
}

QVector<MuPdfDocument::PdfSearchHit> MuPdfDocument::searchDocument(const QString &keyword, QString *error)
{
    QVector<PdfSearchHit> hits;
    if (!m_doc || !m_ctx) {
        return hits;
    }

    if (keyword.trimmed().isEmpty()) {
        return hits;
    }

    const QByteArray needle = keyword.toUtf8();
    fz_stext_options stextOptions = { FZ_STEXT_DEHYPHENATE };
    fz_stext_page *textPage = nullptr;

    for (int pageIndex = 0; pageIndex < pageCount(); ++pageIndex) {
        MuPdfDocumentSearchCollector collector{ &hits, pageIndex };
        fz_try(m_ctx) {
            textPage = fz_new_stext_page_from_page_number(m_ctx, m_doc, pageIndex, &stextOptions);
            if (textPage) {
                fz_match_stext_page_cb(m_ctx, textPage, needle.constData(), MuPdfDocument_collect_search_hits, &collector, FZ_SEARCH_IGNORE_CASE);
                fz_drop_stext_page(m_ctx, textPage);
                textPage = nullptr;
            }
        }
        fz_catch(m_ctx) {
            m_lastError = QString::fromLatin1(fz_caught_message(m_ctx));
            if (textPage) {
                fz_drop_stext_page(m_ctx, textPage);
                textPage = nullptr;
            }
            if (error) {
                *error = m_lastError;
            }
            return hits;
        }
    }

    if (error) {
        *error = m_lastError;
    }
    return hits;
}

QString MuPdfDocument::lastError() const
{
    return m_lastError;
}
