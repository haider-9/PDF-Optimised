
#include <QApplication>
#include <QMainWindow>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsItem>
#include <QGraphicsPixmapItem>
#include <QStyleOptionGraphicsItem>
#include <QGraphicsPathItem>
#include <QPainterPath>
#include <QPainter>
#include <QPen>
#include <QToolBar>
#include <QAction>
#include <QShortcut>
#include <QKeySequence>
#include <QFileDialog>
#include <QMessageBox>
#include <QSpinBox>
#include <QComboBox>
#include <QAbstractSpinBox>
#include <QLineEdit>
#include <QIcon>
#include <QStringList>
#include <QLabel>
#include <QStatusBar>
#include <QThread>
#include <QTimer>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QImage>
#include <QPixmap>
#include <QFileInfo>
#include <QScrollBar>
#include <QHash>
#include <QList>
#include <QColor>
#include <QMetaObject>
#include <QtMath>
#include <QIcon>
#include <QPalette>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QToolButton>
#include <QStackedWidget>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QInputDialog>
#include <QFont>
#include <QFrame>
#include <QPen>
#include <QBrush>
#include <QStyle>
#include <QDesktopServices>
#include <QMetaType>
#include <QUrl>
#include <QKeyEvent>
#include <QGuiApplication>

#include <mupdf/fitz.h>

#include <algorithm>
#include <atomic>
#include <utility>
#include <cmath>

namespace {

// Recolors an icon's pixels to one color, keeping their alpha shape.
// Theme icons are often dark, which disappears on dark palettes.
QIcon tintedIcon(const QIcon &source, const QColor &color)
{
    QIcon result;
    const QList<int> sizes{16, 22, 24, 32};

    for (int size : sizes) {
        for (QIcon::Mode mode : {QIcon::Normal, QIcon::Disabled}) {
            QPixmap pixmap = source.pixmap(size, size, mode);
            if (pixmap.isNull())
                continue;

            QColor tint = color;
            if (mode == QIcon::Disabled)
                tint.setAlpha(110);

            QPainter painter(&pixmap);
            painter.setCompositionMode(
                QPainter::CompositionMode_SourceIn
            );
            painter.fillRect(pixmap.rect(), tint);
            painter.end();

            result.addPixmap(pixmap, mode);
        }
    }

    return result;
}

// Uses the desktop theme icon when available, otherwise the fallback.
// The result is tinted to the active palette's button text color.
QIcon themeIcon(const char *name, const QIcon &fallback = QIcon())
{
    const QIcon icon = QIcon::fromTheme(name);
    const QColor color =
        QApplication::palette().color(QPalette::ButtonText);

    return tintedIcon(icon.isNull() ? fallback : icon, color);
}

void applyDarkTheme(QApplication &app)
{
    app.setStyle("Fusion");

    QPalette palette;
    palette.setColor(QPalette::Window, QColor("#191c21"));
    palette.setColor(QPalette::WindowText, QColor("#e7e9ed"));
    palette.setColor(QPalette::Base, QColor("#121519"));
    palette.setColor(QPalette::AlternateBase, QColor("#20252b"));
    palette.setColor(QPalette::ToolTipBase, QColor("#252b33"));
    palette.setColor(QPalette::ToolTipText, QColor("#f1f3f5"));
    palette.setColor(QPalette::Text, QColor("#e7e9ed"));
    palette.setColor(QPalette::Button, QColor("#252a31"));
    palette.setColor(QPalette::ButtonText, QColor("#e7e9ed"));
    palette.setColor(QPalette::BrightText, QColor("#ffffff"));
    palette.setColor(QPalette::Highlight, QColor("#bd5638"));
    palette.setColor(QPalette::HighlightedText, QColor("#ffffff"));
    palette.setColor(QPalette::PlaceholderText, QColor("#818a96"));
    palette.setColor(QPalette::Disabled, QPalette::Text,
                     QColor("#737b85"));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText,
                     QColor("#737b85"));
    app.setPalette(palette);

    app.setStyleSheet(R"(
        QToolBar {
            background: #20252b;
            border: none;
            border-bottom: 1px solid #343b44;
            spacing: 6px;
            padding: 7px 10px;
        }
        QToolBar::separator {
            background: #3a424c;
            width: 1px;
            margin: 4px 5px;
        }
        QToolButton {
            background: transparent;
            border: 1px solid transparent;
            border-radius: 5px;
            padding: 7px 9px;
        }
        QToolButton:hover, QPushButton:hover {
            background: #303740;
            border-color: #414a55;
        }
        QToolButton:checked {
            background: #493026;
            border-color: #8e4d35;
        }
        QPushButton {
            background: #252a31;
            border: 1px solid #3a424c;
            border-radius: 5px;
            padding: 8px 13px;
        }
        QPushButton#primaryButton {
            background: #bd5638;
            border-color: #bd5638;
            color: #ffffff;
            font-weight: 600;
        }
        QPushButton#primaryButton:hover {
            background: #d06746;
            border-color: #d06746;
        }
        QSpinBox, QLineEdit, QComboBox {
            background: #15191e;
            border: 1px solid #3a424c;
            border-radius: 4px;
            padding: 5px 7px;
            selection-background-color: #bd5638;
        }
        QListWidget {
            background: #15191e;
            border: 1px solid #303740;
            border-radius: 5px;
            padding: 4px;
            outline: none;
        }
        QListWidget::item {
            padding: 11px 10px;
            border-radius: 4px;
        }
        QListWidget::item:hover {
            background: #252b32;
        }
        QListWidget::item:selected {
            background: #493026;
            color: #ffffff;
        }
        QStatusBar {
            background: #20252b;
            border-top: 1px solid #343b44;
        }
        QStatusBar::item {
            border: none;
        }
        QScrollBar:vertical {
            background: #191c21;
            width: 11px;
            margin: 0;
        }
        QScrollBar:horizontal {
            background: #191c21;
            height: 11px;
            margin: 0;
        }
        QScrollBar::handle {
            background: #4a535e;
            border-radius: 4px;
            min-width: 24px;
            min-height: 24px;
        }
        QScrollBar::handle:hover {
            background: #687482;
        }
        QScrollBar::add-line, QScrollBar::sub-line {
            width: 0;
            height: 0;
        }
        QMenu {
            background: #252a31;
            border: 1px solid #3a424c;
            padding: 4px;
        }
        QMenu::item {
            padding: 6px 24px 6px 10px;
            border-radius: 3px;
        }
        QMenu::item:selected {
            background: #493026;
        }
        QToolTip {
            background: #252b33;
            color: #f1f3f5;
            border: 1px solid #414a55;
            padding: 5px;
        }
        QLabel#homeTitle {
            color: #f1f3f5;
            font-size: 25px;
            font-weight: 600;
        }
        QLabel#homeSubtitle {
            color: #9da6b1;
            font-size: 13px;
        }
        QLabel#sectionTitle {
            color: #d4d8de;
            font-size: 14px;
            font-weight: 600;
            padding-top: 12px;
        }
        QLabel#countTotal {
            background: #2b3038;
            border: 1px solid #444d58;
            border-radius: 5px;
            color: #f1f3f5;
            font-weight: 600;
            padding: 6px 10px;
        }
        QLabel#emptyState {
            color: #929ba6;
            padding: 24px;
        }
    )");
}

constexpr int kBaseWidthKey = 2;   // stroke data: pen width before highlight
constexpr qreal kMinStrokePixels = 10.0;  // shorter pencil strokes are discarded

// The full-size render of a page is made at this zoom (times the screen's
// pixel ratio). It is kept, and zooming above fit shows it at once. Zooming
// well past this level will look soft, because it is not re-rendered.
constexpr double kFixedZoom = 4.0;
// Below fit, a smaller bitmap is rendered at this many times the zoom.
constexpr double kZoomHeadroom = 2.0;
// A smaller bitmap is kept while it is at most this many times larger than
// the zoom needs (zooming out), before a smaller one is rendered.
constexpr double kMaxOversample = 3.0;
// Zoom used when [ or ] steps to a pin, so the pin is easy to see.
constexpr double kPinZoom = 2.0;
// Number of parsed pages kept ready for fast re-rendering.
constexpr int kDisplayListCacheSize = 16;

// A clickable area of a page. Positions are fractions (0 to 1) of the page
// size, so they stay valid at any zoom level.
struct PageLink {
    QRectF rect;
    QString uri;               // external target; empty for internal links
    int targetPage = -1;       // internal target page; -1 when none
    QPointF target;            // destination on the target page
    bool hasTarget = false;    // true when the link names a point
};
QColor defaultPenColor()
{
    return QColor("#ff6b35");
}

} // namespace

// Qt needs this outside the anonymous namespace to pass PageLink in signals.
Q_DECLARE_METATYPE(PageLink)

// A numbered pin placed on a page. It belongs to one counter and takes that
// counter's color. Its fraction is its position relative to the page, so it
// can be found again at any zoom or page size.
class CountMarkerItem : public QGraphicsItem
{
public:
    CountMarkerItem(int number, int counter, const QColor &color,
                    bool preview = false)
        : m_number(number), m_counter(counter), m_color(color),
          m_preview(preview)
    {
        setFlag(QGraphicsItem::ItemIgnoresTransformations);
        setAcceptedMouseButtons(Qt::NoButton);
    }

    QRectF boundingRect() const override
    {
        return QRectF(-22, -48, 44, 50);
    }

    int counter() const
    {
        return m_counter;
    }

    // A highlighted pin gets a ring, so the one being stepped to stands out.
    void setHighlighted(bool on)
    {
        if (m_highlighted == on)
            return;
        m_highlighted = on;
        update();
    }

    void setNumber(int number)
    {
        if (m_number == number)
            return;
        m_number = number;
        update();
    }

    void setColor(const QColor &color)
    {
        m_color = color;
        update();
    }

    QPointF fraction() const
    {
        return m_fraction;
    }

    void setFraction(const QPointF &fraction)
    {
        m_fraction = fraction;
    }

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *) override
    {
        painter->setRenderHint(QPainter::Antialiasing);
        QColor fill = m_color;
        if (m_preview)
            fill.setAlpha(165);

        if (m_highlighted) {
            painter->setPen(QPen(QColor("#ffd600"), 3));
            painter->setBrush(Qt::NoBrush);
            painter->drawEllipse(QPointF(0, -26), 19, 19);
        }

        QPainterPath pin;
        pin.moveTo(-7, -16);
        pin.lineTo(0, 0);
        pin.lineTo(7, -16);
        pin.closeSubpath();

        QPen outline(QColor("#ffffff"), 1.5);
        painter->setPen(outline);
        painter->setBrush(fill);
        painter->drawPath(pin);
        painter->drawEllipse(QRectF(-13, -39, 26, 26));

        painter->setPen(Qt::white);
        QFont labelFont = painter->font();
        labelFont.setBold(true);
        labelFont.setPixelSize(m_number > 99 ? 9 : 11);
        painter->setFont(labelFont);
        painter->drawText(QRectF(-12, -38, 24, 24), Qt::AlignCenter,
                          QString::number(m_number));
    }

private:
    int m_number = 1;
    int m_counter = -1;
    QColor m_color;
    QPointF m_fraction;
    bool m_highlighted = false;
    bool m_preview = false;
};

// Maps a rectangle given in page coordinates to fractions of the page.
QRectF normalizedRect(fz_rect rect, fz_rect bounds)
{
    const double w = bounds.x1 - bounds.x0;
    const double h = bounds.y1 - bounds.y0;
    if (w <= 0 || h <= 0)
        return QRectF();

    return QRectF(
        QPointF((rect.x0 - bounds.x0) / w, (rect.y0 - bounds.y0) / h),
        QPointF((rect.x1 - bounds.x0) / w, (rect.y1 - bounds.y0) / h)
    ).normalized();
}

class PdfWorker : public QObject
{
    Q_OBJECT

public:
    std::atomic<quint64> latestRequest{0};
    std::atomic<quint64> latestOpen{0};

    ~PdfWorker() override
    {
        closeDocument();
    }

public slots:
    void openDocument(const QString &path, quint64 id)
    {
        // A newer open was requested while this one was queued: skip it.
        if (id != latestOpen.load())
            return;

        closeDocument();

        const QByteArray encoded = path.toUtf8();

        ctx = fz_new_context(nullptr, nullptr, 256 * 1024 * 1024);

        if (!ctx) {
            emit openFailed("Could not create the MuPDF context.", id);
            return;
        }

        fz_register_document_handlers(ctx);

        fz_try(ctx) {
            doc = fz_open_document(ctx, encoded.constData());
            pageCount = fz_count_pages(ctx, doc);
        }
        fz_catch(ctx) {
            const QString message =
                QString::fromUtf8(fz_caught_message(ctx));

            closeDocument();
            emit openFailed("Could not open PDF: " + message, id);
            return;
        }

        if (pageCount < 1) {
            closeDocument();
            emit openFailed("The PDF contains no pages.", id);
            return;
        }

        // A newer open arrived while this file was being parsed: discard it.
        if (id != latestOpen.load()) {
            closeDocument();
            return;
        }

        emit documentOpened(pageCount, path, id);
    }

    void closeDocument()
    {
        if (!ctx)
            return;

        prefetchQueue.clear();
        dropDisplayLists();

        if (doc) {
            fz_drop_document(ctx, doc);
            doc = nullptr;
        }

        fz_drop_context(ctx);
        ctx = nullptr;
        pageCount = 0;
    }

    // Stops the render in progress, if any. Called from the GUI thread;
    // MuPDF checks the cookie while it draws, so this only sets a flag.
    void cancelRender()
    {
        m_cookie.abort = 1;
    }

    void renderPage(int pageIndex, double requestedScale, quint64 id,
                    bool wantPreview)
    {
        if (id != latestRequest.load() || !ctx || !doc)
            return;

        if (pageIndex < 0 || pageIndex >= pageCount)
            return;

        m_cookie.abort = 0;

        // Progressive render: when the page has nothing sharp on screen yet,
        // a quarter-size preview appears quickly, then the full-resolution
        // render replaces it. Each pass checks whether a newer request has
        // arrived, so stale work stops early.
        const double finalScale = qBound(0.15, requestedScale, 8.0);
        if (wantPreview) {
            renderPass(pageIndex, finalScale * 0.5, id, true);

            if (id != latestRequest.load() || m_cookie.abort != 0)
                return;
        }

        // Parsing the page is done once; later zooms only rasterize.
        displayListFor(pageIndex);

        if (id != latestRequest.load() || m_cookie.abort != 0)
            return;

        renderPass(pageIndex, finalScale, id, false);

        // Parse the neighbouring pages while the user looks at this one.
        schedulePrefetch(pageIndex);
    }

    // Reads the clickable links of a page. Positions are returned as
    // fractions of the page size, and internal targets are resolved to a
    // page index and a point on that page.
    void loadLinks(int pageIndex, quint64 id)
    {
        if (!ctx || !doc || pageIndex < 0 || pageIndex >= pageCount)
            return;

        QList<PageLink> result;
        QHash<int, fz_rect> targetBounds;
        fz_page *volatile page = nullptr;
        fz_link *volatile links = nullptr;
        volatile bool ok = false;

        fz_try(ctx) {
            page = fz_load_page(ctx, doc, pageIndex);
            const fz_rect bounds = fz_bound_page(ctx, page);
            links = fz_load_links(ctx, page);

            for (fz_link *link = links; link; link = link->next) {
                if (!link->uri)
                    continue;

                PageLink item;
                item.rect = normalizedRect(link->rect, bounds);
                if (item.rect.isEmpty())
                    continue;

                if (fz_is_external_link(ctx, link->uri)) {
                    item.uri = QString::fromUtf8(link->uri);
                    result.append(item);
                    continue;
                }

                float xp = 0.0f;
                float yp = 0.0f;
                const fz_location location =
                    fz_resolve_link(ctx, doc, link->uri, &xp, &yp);
                if (location.page < 0)
                    continue;

                const int target =
                    fz_page_number_from_location(ctx, doc, location);
                if (target < 0 || target >= pageCount)
                    continue;

                item.targetPage = target;

                fz_rect targetRect = {0, 0, 0, 0};
                if (!targetBounds.contains(target)) {
                    if (pageBounds(target, &targetRect))
                        targetBounds.insert(target, targetRect);
                } else {
                    targetRect = targetBounds.value(target);
                }

                const double w = targetRect.x1 - targetRect.x0;
                const double h = targetRect.y1 - targetRect.y0;
                if (w > 0 && h > 0 && std::isfinite(xp) && std::isfinite(yp)) {
                    item.target = QPointF(
                        (xp - targetRect.x0) / w,
                        (yp - targetRect.y0) / h
                    );
                    item.hasTarget = true;
                }

                result.append(item);
            }

            ok = true;
        }
        fz_always(ctx) {
            if (links)
                fz_drop_link(ctx, links);
            if (page)
                fz_drop_page(ctx, page);
        }
        fz_catch(ctx) {
            ok = false;
        }

        if (ok)
            emit linksLoaded(result, pageIndex, id);
    }

    // Returns the page's bounds, or false if the page cannot be loaded.
    bool pageBounds(int pageIndex, fz_rect *bounds)
    {
        fz_page *volatile page = nullptr;
        volatile bool ok = false;

        fz_try(ctx) {
            page = fz_load_page(ctx, doc, pageIndex);
            *bounds = fz_bound_page(ctx, page);
            ok = true;
        }
        fz_always(ctx) {
            if (page)
                fz_drop_page(ctx, page);
        }
        fz_catch(ctx) {
            ok = false;
        }

        return ok;
    }

    void dropDisplayLists()
    {
        for (fz_display_list *list : displayLists)
            fz_drop_display_list(ctx, list);

        displayLists.clear();
        displayListOrder.clear();
    }

    // Returns the parsed page, building it if needed. Rasterizing from a
    // display list is much faster than re-reading the page content for every
    // zoom level. Returns nullptr if the page cannot be parsed.
    fz_display_list *displayListFor(int pageIndex)
    {
        if (fz_display_list *cached = displayLists.value(pageIndex, nullptr)) {
            // Move the page to the most recently used end.
            displayListOrder.removeAll(pageIndex);
            displayListOrder.append(pageIndex);
            return cached;
        }

        fz_page *volatile page = nullptr;
        fz_display_list *volatile built = nullptr;

        fz_try(ctx) {
            page = fz_load_page(ctx, doc, pageIndex);
            built = fz_new_display_list_from_page(ctx, page);
        }
        fz_always(ctx) {
            if (page)
                fz_drop_page(ctx, page);
        }
        fz_catch(ctx) {
            built = nullptr;
        }

        fz_display_list *list = built;
        if (!list)
            return nullptr;

        // Keep the cache small: drop the least recently used page first.
        while (displayListOrder.size() >= kDisplayListCacheSize) {
            const int oldest = displayListOrder.takeFirst();
            fz_drop_display_list(ctx, displayLists.take(oldest));
        }

        displayLists.insert(pageIndex, list);
        displayListOrder.append(pageIndex);
        return list;
    }

    // Parses the pages around the one on screen, one page per queued step,
    // so pending render requests always run first. Neighbours are read in
    // order of distance from the current page. The window fits in the cache
    // (the current page plus two windows of neighbours), so the pages near
    // the user are never evicted by their own prefetch.
    void schedulePrefetch(int centerPage)
    {
        prefetchQueue.clear();

        for (int distance = 1; distance <= kDisplayListCacheSize / 2 - 1;
             ++distance) {
            for (int page : {centerPage + distance, centerPage - distance}) {
                if (page >= 0 && page < pageCount)
                    prefetchQueue.append(page);
            }
        }

        if (!prefetchRunning) {
            prefetchRunning = true;
            QMetaObject::invokeMethod(this, [this] { prefetchStep(); },
                                      Qt::QueuedConnection);
        }
    }

    void prefetchStep()
    {
        prefetchRunning = false;

        if (!ctx || !doc)
            return;

        // Skip pages that are already parsed, then build the next one.
        while (!prefetchQueue.isEmpty()) {
            const int page = prefetchQueue.takeFirst();
            if (!displayLists.contains(page)) {
                displayListFor(page);
                break;
            }
        }

        if (!prefetchQueue.isEmpty()) {
            prefetchRunning = true;
            QMetaObject::invokeMethod(this, [this] { prefetchStep(); },
                                      Qt::QueuedConnection);
        }
    }

    // Draws one page at a scale into a buffer owned by the emitted image.
    void renderPass(int pageIndex, double requestedScale, quint64 id,
                    bool preview)
    {
        double scale = requestedScale;
        QImage image;
        int pixX = 0;
        int pixY = 0;
        QString failure;

        fz_page *volatile page = nullptr;
        fz_device *volatile device = nullptr;
        fz_pixmap *volatile pix = nullptr;
        fz_display_list *list = nullptr;
        volatile bool ok = false;

        // Use the parsed page when it is already available. A preview does
        // not wait for the parse; the final pass parses first.
        list = displayLists.value(pageIndex, nullptr);

        fz_try(ctx) {
            fz_rect bounds = {0, 0, 0, 0};

            if (list) {
                bounds = fz_bound_display_list(ctx, list);
            } else {
                page = fz_load_page(ctx, doc, pageIndex);
                bounds = fz_bound_page(ctx, page);
            }

            const double width = bounds.x1 - bounds.x0;
            const double height = bounds.y1 - bounds.y0;

            if (width <= 0 || height <= 0)
                fz_throw(ctx, FZ_ERROR_GENERIC, "Invalid page dimensions");

            // Limit each rendered page to approximately 18 megapixels.
            constexpr double maxPixels = 18.0 * 1024.0 * 1024.0;
            const double pixels = width * height * scale * scale;

            if (pixels > maxPixels)
                scale *= std::sqrt(maxPixels / pixels);

            const fz_matrix ctm = fz_scale(float(scale), float(scale));
            const fz_irect box = fz_round_rect(fz_transform_rect(bounds, ctm));
            const int w = box.x1 - box.x0;
            const int h = box.y1 - box.y0;

            // MuPDF draws straight into the image's own buffer, so the page
            // is never copied after rendering.
            image = QImage(w, h, QImage::Format_RGB888);
            if (image.isNull())
                fz_throw(ctx, FZ_ERROR_GENERIC, "Not enough memory");

            pix = fz_new_pixmap_with_data(
                ctx,
                fz_device_rgb(ctx),
                w,
                h,
                nullptr,
                0,
                image.bytesPerLine(),
                image.bits()
            );
            fz_clear_pixmap_with_value(ctx, pix, 0xff);

            const fz_matrix transform = fz_concat(
                ctm,
                fz_translate(float(-box.x0), float(-box.y0))
            );

            device = fz_new_draw_device(ctx, fz_identity, pix);
            if (list)
                fz_run_display_list(ctx, list, device, transform,
                                    fz_infinite_rect, &m_cookie);
            else
                fz_run_page(ctx, page, device, transform, &m_cookie);
            fz_close_device(ctx, device);

            pixX = box.x0;
            pixY = box.y0;
            ok = true;
        }
        fz_always(ctx) {
            if (device)
                fz_drop_device(ctx, device);
            if (pix)
                fz_drop_pixmap(ctx, pix);
            if (page)
                fz_drop_page(ctx, page);
        }
        fz_catch(ctx) {
            ok = false;
            failure = QString::fromUtf8(fz_caught_message(ctx));
        }

        // A cancelled render is normal during fast zooming: stay silent.
        const bool cancelled = m_cookie.abort != 0;

        if (ok && id == latestRequest.load() && !image.isNull()) {
            emit pageRendered(image, pageIndex, id, scale, pixX, pixY,
                              preview);
        } else if (!ok && !cancelled && id == latestRequest.load()) {
            emit errorOccurred("Rendering failed: " + failure);
        }
    }

signals:
    void documentOpened(int pageCount, const QString &path, quint64 openId);

    void openFailed(const QString &message, quint64 openId);

    void pageRendered(
        const QImage &image,
        int pageIndex,
        quint64 requestId,
        double renderScale,
        int pixelX,
        int pixelY,
        bool preview
    );

    void errorOccurred(const QString &message);

    void linksLoaded(const QList<PageLink> &links, int pageIndex,
                     quint64 requestId);

private:
    fz_context *ctx = nullptr;
    fz_document *doc = nullptr;
    fz_cookie m_cookie{};
    // Parsed pages, reused by zooms. Oldest first in displayListOrder.
    QHash<int, fz_display_list *> displayLists;
    QList<int> displayListOrder;
    QList<int> prefetchQueue;
    bool prefetchRunning = false;
    int pageCount = 0;
};


class PdfCanvas : public QGraphicsView
{
    Q_OBJECT

public:
    explicit PdfCanvas(
        QGraphicsScene *scene,
        QWidget *parent = nullptr
    ) : QGraphicsView(scene, parent)
    {
        setRenderHint(QPainter::SmoothPixmapTransform);
        setBackgroundBrush(QColor("#343434"));

        setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
        setResizeAnchor(QGraphicsView::AnchorUnderMouse);

        setDragMode(QGraphicsView::ScrollHandDrag);
        setFrameShape(QFrame::NoFrame);

        // Needed to report hover positions when no button is held.
        setMouseTracking(true);
    }

    // Shows a pointing hand over links. Pencil mode and middle-button
    // panning keep their own cursors.
    void setLinkCursor(bool on)
    {
        if (m_linkCursor == on)
            return;

        m_linkCursor = on;

        if (m_pencilMode || m_countMode || middlePanning)
            return;

        // The cursor must be set on the viewport: it is the child widget
        // that receives the mouse and shows the open-hand pan cursor.
        if (on)
            viewport()->setCursor(Qt::PointingHandCursor);
        else if (dragMode() == QGraphicsView::ScrollHandDrag)
            viewport()->setCursor(Qt::OpenHandCursor);
        else
            viewport()->unsetCursor();
    }

    ~PdfCanvas() override
    {
        // Undone strokes are owned here, not by the scene.
        discardRedo();
    }

    double zoomLevel() const
    {
        return transform().m11();
    }

    bool pencilMode() const
    {
        return m_pencilMode;
    }

    bool countMode() const
    {
        return m_countMode;
    }

    void setPencilMode(bool enabled)
    {
        if (enabled && m_countMode)
            setCountMode(false);

        m_pencilMode = enabled;
        setDragMode(
            enabled
                ? QGraphicsView::NoDrag
                : QGraphicsView::ScrollHandDrag
        );

        updateInteractionCursor();

        emit pencilModeChanged(enabled);
    }

    void setCountMode(bool enabled)
    {
        if (enabled && m_pencilMode) {
            m_pencilMode = false;
            emit pencilModeChanged(false);
        }

        if (m_countMode == enabled)
            return;

        m_countMode = enabled;
        setDragMode(enabled || m_pencilMode
            ? QGraphicsView::NoDrag
            : QGraphicsView::ScrollHandDrag);
        if (enabled && !m_countPreview) {
            m_countPreview = new CountMarkerItem(1, m_activeCounter,
                                                 m_activeColor, true);
            m_countPreview->setZValue(30);
            scene()->addItem(m_countPreview);
        }

        if (m_countPreview)
            m_countPreview->setVisible(enabled);

        updateInteractionCursor();
        emit countModeChanged(enabled);
    }

    // Chooses the counter that new pins belong to. Id -1 means none.
    void setActiveCounter(int id, const QColor &color)
    {
        m_activeCounter = id;
        m_activeColor = color;

        if (m_countPreview) {
            m_countPreview->setColor(color);
            m_countPreview->setNumber(countOn(m_currentPage, id) + 1);
        }
    }

    int activeCounter() const
    {
        return m_activeCounter;
    }

    // Pins of every counter, on every page.
    int pinCount() const
    {
        return m_markerOrder.size();
    }

    // Pins of one counter on one page.
    int countOn(int page, int counter) const
    {
        int total = 0;
        for (const CountMarkerItem *marker : m_countMarkers.value(page)) {
            if (marker->counter() == counter)
                ++total;
        }
        return total;
    }

    // Pins of one counter on every page.
    int countTotal(int counter) const
    {
        int total = 0;
        for (auto it = m_countMarkers.cbegin(); it != m_countMarkers.cend(); ++it) {
            for (const CountMarkerItem *marker : it.value()) {
                if (marker->counter() == counter)
                    ++total;
            }
        }
        return total;
    }

    // Pins of one counter on one page, in the order they were placed.
    QList<CountMarkerItem *> markersOn(int page, int counter) const
    {
        QList<CountMarkerItem *> result;
        for (CountMarkerItem *marker : m_countMarkers.value(page)) {
            if (marker->counter() == counter)
                result.append(marker);
        }
        return result;
    }

    // Pins of one counter in page order, with their page.
    QList<QPair<int, CountMarkerItem *>> markersFor(int counter) const
    {
        QList<int> pages = m_countMarkers.keys();
        std::sort(pages.begin(), pages.end());

        QList<QPair<int, CountMarkerItem *>> result;
        for (int page : pages) {
            for (CountMarkerItem *marker : m_countMarkers.value(page)) {
                if (marker->counter() == counter)
                    result.append({page, marker});
            }
        }
        return result;
    }

    // Removes every pin of a counter, for when the counter is deleted.
    void removeCounterMarkers(int counter)
    {
        for (auto it = m_countMarkers.begin(); it != m_countMarkers.end(); ++it) {
            QList<CountMarkerItem *> kept;
            for (CountMarkerItem *marker : it.value()) {
                if (marker->counter() == counter) {
                    m_markerOrder.removeAll(marker);
                    scene()->removeItem(marker);
                    delete marker;
                } else {
                    kept.append(marker);
                }
            }
            it.value() = kept;
        }

        renumberMarkers();
        emit countChanged();
    }

    // Removes the most recently placed pin. Returns false if there is none.
    bool undoLastMarker()
    {
        if (m_markerOrder.isEmpty())
            return false;

        CountMarkerItem *last = m_markerOrder.takeLast();
        for (auto it = m_countMarkers.begin(); it != m_countMarkers.end(); ++it)
            it.value().removeAll(last);

        scene()->removeItem(last);
        delete last;

        renumberMarkers();
        emit countChanged();
        return true;
    }

    // Numbers each counter's pins 1, 2, 3... in order on every page.
    void renumberMarkers()
    {
        for (auto it = m_countMarkers.begin(); it != m_countMarkers.end(); ++it) {
            QHash<int, int> seen;
            for (CountMarkerItem *marker : it.value())
                marker->setNumber(++seen[marker->counter()]);
        }
    }

    void updateCountPreview(const QPointF &scenePos)
    {
        if (!m_countMode || !m_countPreview)
            return;

        m_countPreview->setNumber(countOn(m_currentPage, m_activeCounter) + 1);
        m_countPreview->setPos(scenePos);
        m_countPreview->setVisible(scene()->sceneRect().contains(scenePos));
    }

    void clearCountMarkers()
    {
        for (auto it = m_countMarkers.begin(); it != m_countMarkers.end(); ++it) {
            for (CountMarkerItem *marker : it.value()) {
                scene()->removeItem(marker);
                delete marker;
            }
        }
        m_countMarkers.clear();
        m_markerOrder.clear();
        if (m_countPreview)
            m_countPreview->setNumber(1);
        emit countChanged();
    }

    void setCurrentPage(int page)
    {
        m_currentPage = page;
        for (auto it = m_annotations.begin();
             it != m_annotations.end(); ++it) {
            for (auto *item : it.value())
                item->setVisible(it.key() == page);
        }

        for (auto it = m_countMarkers.begin();
             it != m_countMarkers.end(); ++it) {
            for (CountMarkerItem *marker : it.value())
                marker->setVisible(it.key() == page);
        }

        if (m_countPreview)
            m_countPreview->setVisible(false);

        emit countChanged();
    }

    // Removes the most recently drawn stroke. Returns false if none.
    bool undoLastStroke()
    {
        if (m_drawing || m_strokeOrder.isEmpty())
            return false;

        const QPair<int, QGraphicsPathItem *> last = m_strokeOrder.takeLast();
        m_annotations[last.first].removeAll(last.second);

        if (m_highlighted == last.second)
            m_highlighted = nullptr;

        scene()->removeItem(last.second);
        m_redoStack.append(last);

        return true;
    }

    // Puts back the most recently undone stroke. Returns false if none.
    bool redoLastStroke()
    {
        if (m_drawing || m_redoStack.isEmpty())
            return false;

        const QPair<int, QGraphicsPathItem *> stroke = m_redoStack.takeLast();
        scene()->addItem(stroke.second);
        stroke.second->setVisible(stroke.first == m_currentPage);
        m_annotations[stroke.first].append(stroke.second);
        m_strokeOrder.append(stroke);

        return true;
    }

    // A stroke must span at least kMinStrokePixels on screen to count.
    // Shorter ones are accidental clicks and are dropped.
    bool isLongEnough(const QGraphicsPathItem *stroke) const
    {
        if (!stroke)
            return false;

        const QRectF bounds = stroke->path().boundingRect();
        const qreal screenLength = std::hypot(bounds.width(), bounds.height())
            * transform().m11();

        return screenLength >= kMinStrokePixels;
    }

    // Removes the stroke drawn last, before it reached the undo history.
    void discardLastStroke()
    {
        if (m_strokeOrder.isEmpty())
            return;

        const QPair<int, QGraphicsPathItem *> last = m_strokeOrder.takeLast();
        m_annotations[last.first].removeAll(last.second);

        if (m_highlighted == last.second)
            m_highlighted = nullptr;

        scene()->removeItem(last.second);
        delete last.second;
    }

    // Frees undone strokes; called once a new stroke replaces them.
    void discardRedo()
    {
        for (const auto &entry : m_redoStack)
            delete entry.second;
        m_redoStack.clear();
    }

    // Draws one stroke thicker so the user can see which line is focused.
    void highlightStroke(QGraphicsPathItem *item)
    {
        auto setWidth = [](QGraphicsPathItem *stroke, double factor) {
            QPen pen = stroke->pen();
            pen.setWidthF(stroke->data(kBaseWidthKey).toDouble() * factor);
            stroke->setPen(pen);
        };

        if (m_highlighted)
            setWidth(m_highlighted, 1.0);

        m_highlighted = item;

        if (item)
            setWidth(item, 3.0);
    }

    void clearAnnotations()
    {
        for (auto it = m_annotations.begin();
             it != m_annotations.end(); ++it) {
            for (auto *item : it.value()) {
                scene()->removeItem(item);
                delete item;
            }
        }

        m_annotations.clear();
        m_strokeOrder.clear();
        discardRedo();
        m_highlighted = nullptr;
        m_currentPathItem = nullptr;
        m_drawing = false;
    }

signals:
    void zoomChanged(double zoom);
    void pencilModeChanged(bool enabled);
    void countModeChanged(bool enabled);
    void countChanged();
    void viewportResized();
    void linkClicked(const QPointF &scenePos);
    void hovered(const QPointF &scenePos);

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QGraphicsView::resizeEvent(event);
        emit viewportResized();
    }

    void wheelEvent(QWheelEvent *event) override
    {
        const double steps = event->angleDelta().y() / 120.0;

        if (qFuzzyIsNull(steps)) {
            event->accept();
            return;
        }

        const double oldZoom = zoomLevel();
        const double newZoom = qBound(
            0.05,
            oldZoom * std::pow(1.20, steps),
            8.0
        );

        if (!qFuzzyCompare(oldZoom, newZoom))
            scale(newZoom / oldZoom, newZoom / oldZoom);

        emit zoomChanged(zoomLevel());
        event->accept();
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (m_pencilMode && event->button() == Qt::LeftButton) {
            m_drawing = true;
            discardRedo();

            m_currentPath = QPainterPath(
                mapToScene(event->position().toPoint())
            );

            QPen pen(
                m_penColor,
                2.0 / qMax(zoomLevel(), 0.05),
                Qt::SolidLine,
                Qt::RoundCap,
                Qt::RoundJoin
            );

            m_currentPathItem = scene()->addPath(
                m_currentPath,
                pen
            );

            m_currentPathItem->setZValue(10);
            m_currentPathItem->setData(kBaseWidthKey, pen.widthF());
            m_annotations[m_currentPage].append(m_currentPathItem);
            m_strokeOrder.append({m_currentPage, m_currentPathItem});

            event->accept();
            return;
        }

        if (event->button() == Qt::MiddleButton) {
            middlePanning = true;
            lastMousePosition = event->position().toPoint();
            updateInteractionCursor();
            event->accept();
            return;
        }

        if (event->button() == Qt::LeftButton) {
            m_clickStart = event->position().toPoint();
            m_clickPending = true;
        }

        QGraphicsView::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (m_drawing && m_currentPathItem) {
            m_currentPath.lineTo(
                mapToScene(event->position().toPoint())
            );

            m_currentPathItem->setPath(m_currentPath);

            event->accept();
            return;
        }

        if (middlePanning) {
            const QPoint delta =
                event->position().toPoint() - lastMousePosition;

            lastMousePosition = event->position().toPoint();

            horizontalScrollBar()->setValue(
                horizontalScrollBar()->value() - delta.x()
            );

            verticalScrollBar()->setValue(
                verticalScrollBar()->value() - delta.y()
            );

            event->accept();
            return;
        }

        emit hovered(mapToScene(event->position().toPoint()));
        QGraphicsView::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && m_drawing) {
            m_drawing = false;
            if (!isLongEnough(m_currentPathItem))
                discardLastStroke();
            m_currentPathItem = nullptr;
            event->accept();
            return;
        }

        if (event->button() == Qt::MiddleButton && middlePanning) {
            middlePanning = false;
            updateInteractionCursor();

            event->accept();
            return;
        }

        if (event->button() == Qt::LeftButton && m_clickPending) {
            m_clickPending = false;

            const QPoint moved = event->position().toPoint() - m_clickStart;
            if (moved.manhattanLength() < 4) {
                const QPointF scenePos = mapToScene(event->position().toPoint());
                if (m_countMode) {
                    const QRectF page = scene()->sceneRect();
                    if (m_activeCounter >= 0 && page.contains(scenePos)) {
                        const QPointF fraction(
                            (scenePos.x() - page.left()) / page.width(),
                            (scenePos.y() - page.top()) / page.height()
                        );

                        const int number =
                            countOn(m_currentPage, m_activeCounter) + 1;
                        auto *marker = new CountMarkerItem(
                            number, m_activeCounter, m_activeColor);
                        marker->setPos(scenePos);
                        marker->setFraction(fraction);
                        marker->setZValue(20);
                        scene()->addItem(marker);
                        m_countMarkers[m_currentPage].append(marker);
                        m_markerOrder.append(marker);
                        emit countChanged();
                        updateCountPreview(scenePos);
                    }
                    event->accept();
                    return;
                }

                if (!m_pencilMode)
                    emit linkClicked(scenePos);
            }
        }

        QGraphicsView::mouseReleaseEvent(event);
    }

private:
    void updateInteractionCursor()
    {
        if (m_countMode)
            viewport()->setCursor(Qt::BlankCursor);
        else if (middlePanning)
            viewport()->setCursor(Qt::ClosedHandCursor);
        else if (m_pencilMode)
            viewport()->setCursor(Qt::CrossCursor);
        else if (m_linkCursor)
            viewport()->setCursor(Qt::PointingHandCursor);
        else if (dragMode() == QGraphicsView::ScrollHandDrag)
            viewport()->setCursor(Qt::OpenHandCursor);
        else
            viewport()->unsetCursor();
    }

    bool m_pencilMode = false;
    bool m_countMode = false;
    bool m_drawing = false;
    bool middlePanning = false;
    bool m_clickPending = false;
    QPoint m_clickStart;
    bool m_linkCursor = false;

    int m_currentPage = 0;
    int m_activeCounter = -1;
    QColor m_activeColor;
    // Every pin in the order it was placed, for undo.
    QList<CountMarkerItem *> m_markerOrder;
    QPoint lastMousePosition;

    QPainterPath m_currentPath;
    QGraphicsPathItem *m_currentPathItem = nullptr;

    QHash<int, QList<QGraphicsPathItem *>> m_annotations;
    QHash<int, QList<CountMarkerItem *>> m_countMarkers;
    CountMarkerItem *m_countPreview = nullptr;

    // Strokes in the order they were drawn, for undo.
    QList<QPair<int, QGraphicsPathItem *>> m_strokeOrder;

    // Undone strokes, kept off the scene so they can be redone.
    QList<QPair<int, QGraphicsPathItem *>> m_redoStack;

    QColor m_penColor = defaultPenColor();
    QGraphicsPathItem *m_highlighted = nullptr;
};


// Start page listing recently opened PDFs that still exist on disk.
class HomePage : public QWidget
{
    Q_OBJECT

public:
    explicit HomePage(QWidget *parent = nullptr) : QWidget(parent)
    {
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(48, 40, 48, 40);
        layout->setSpacing(16);

        auto *title = new QLabel("CanvasPDF", this);
        title->setObjectName("homeTitle");
        QFont titleFont = title->font();
        titleFont.setPointSize(25);
        titleFont.setBold(true);
        title->setFont(titleFont);
        layout->addWidget(title);

        auto *subtitle = new QLabel(
            "A focused space for your documents.", this
        );
        subtitle->setObjectName("homeSubtitle");
        layout->addWidget(subtitle);

        auto *recentTitle = new QLabel("Recently opened", this);
        recentTitle->setObjectName("sectionTitle");
        layout->addWidget(recentTitle);

        m_list = new QListWidget(this);
        m_list->setIconSize(QSize(32, 32));
        m_list->setSpacing(2);
        // A single click opens the file; Enter and double-click also
        // activate it. Repeat clicks are ignored by the window while the
        // file is still loading.
        auto openItem = [this](QListWidgetItem *item) {
            if (item)
                emit openRequested(item->data(Qt::UserRole).toString());
        };
        connect(m_list, &QListWidget::itemClicked, this, openItem);
        connect(m_list, &QListWidget::itemActivated, this, openItem);
        layout->addWidget(m_list, 1);

        m_empty = new QLabel(
            "No recent PDFs yet. Open one to get started.",
            this
        );
        m_empty->setObjectName("emptyState");
        m_empty->setAlignment(Qt::AlignCenter);
        layout->addWidget(m_empty, 1);

        auto *openButton = new QPushButton(
            themeIcon("document-open",
                      style()->standardIcon(QStyle::SP_DialogOpenButton)),
            "Open PDF",
            this
        );
        openButton->setObjectName("primaryButton");
        connect(openButton, &QPushButton::clicked,
                this, &HomePage::openDialogRequested);
        layout->addWidget(openButton, 0, Qt::AlignLeft);
    }

    // Lists the given paths, leaving out files that are no longer present.
    void setRecentFiles(const QStringList &paths)
    {
        m_list->clear();

        for (const QString &path : paths) {
            const QFileInfo info(path);
            if (!info.isFile())
                continue;

            auto *item = new QListWidgetItem(
                themeIcon("application-pdf",
                          style()->standardIcon(QStyle::SP_FileIcon)),
                info.fileName() + "\n" + info.absolutePath()
            );
            item->setToolTip(info.absoluteFilePath());
            item->setData(Qt::UserRole, info.absoluteFilePath());
            m_list->addItem(item);
        }

        const bool hasFiles = m_list->count() > 0;
        m_list->setVisible(hasFiles);
        m_empty->setVisible(!hasFiles);
    }

signals:
    void openRequested(const QString &path);
    void openDialogRequested();

private:
    QListWidget *m_list = nullptr;
    QLabel *m_empty = nullptr;
};


// Shows a thumbnail of the current page with a rectangle marking the
// visible area. Interactive maps pan the canvas when dragged; the small
// one only reports clicks so the window can open the enlarged map.
class MiniMap : public QWidget
{
    Q_OBJECT

public:
    explicit MiniMap(QWidget *parent = nullptr) : QWidget(parent)
    {
        setExpanded(false);
    }

    bool isExpanded() const
    {
        return m_expanded;
    }

    void setExpanded(bool expanded)
    {
        m_expanded = expanded;
        setFixedSize(expanded ? 380 : 96, expanded ? 380 : 60);
        setCursor(expanded ? Qt::OpenHandCursor : Qt::PointingHandCursor);
        update();
    }

    void setThumbnail(const QPixmap &thumbnail)
    {
        m_thumbnail = thumbnail;
        update();
    }

    // Visible area in page-normalized coordinates, where (0,0)-(1,1)
    // is the whole page.
    void setViewport(const QRectF &viewport)
    {
        m_viewport = viewport;
        update();
    }

signals:
    // Emitted when the user clicks the map (collapsed) or its close corner.
    void toggled();
    void panRequested(const QPointF &center);

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);

        const QPalette pal = palette();
        painter.fillRect(rect(), pal.color(QPalette::Base));

        const QRectF image = imageRect();
        if (image.isEmpty())
            return;

        painter.drawPixmap(
            image,
            m_thumbnail,
            QRectF(m_thumbnail.rect())
        );

        painter.setClipRect(image);

        const QRectF view(
            image.left() + m_viewport.left() * image.width(),
            image.top() + m_viewport.top() * image.height(),
            m_viewport.width() * image.width(),
            m_viewport.height() * image.height()
        );

        const QColor accent = pal.color(QPalette::Highlight);
        QColor fill = accent;
        fill.setAlpha(60);

        painter.setPen(QPen(accent, 2));
        painter.setBrush(fill);
        painter.drawRect(view);

        painter.setClipping(false);
        painter.setPen(QPen(pal.color(QPalette::Mid), 1));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(rect().adjusted(0, 0, -1, -1));

        if (m_expanded) {
            // Close glyph in the top-right corner.
            const QRectF close = closeRect().adjusted(6, 6, -6, -6);
            painter.setPen(QPen(pal.color(QPalette::ButtonText), 2));
            painter.drawLine(close.topLeft(), close.bottomRight());
            painter.drawLine(close.topRight(), close.bottomLeft());
        }
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton)
            return;

        if (!m_expanded) {
            m_pressed = true;
            return;
        }

        if (closeRect().contains(event->position())) {
            m_pressed = true;
            return;
        }

        if (imageRect().isEmpty())
            return;

        const QPointF point = normalized(event->position());

        m_dragging = true;
        m_grabOffset = m_viewport.contains(point)
            ? point - m_viewport.center()
            : QPointF();

        if (!m_viewport.contains(point))
            emit panRequested(clamped(point));

        setCursor(Qt::ClosedHandCursor);
        event->accept();
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (!m_dragging)
            return;

        emit panRequested(
            clamped(normalized(event->position()) - m_grabOffset)
        );
        event->accept();
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton)
            return;

        if (!m_expanded) {
            if (m_pressed && rect().contains(event->position().toPoint()))
                emit toggled();

            m_pressed = false;
            return;
        }

        if (m_pressed) {
            m_pressed = false;
            if (closeRect().contains(event->position()))
                emit toggled();
            return;
        }

        m_dragging = false;
        setCursor(Qt::OpenHandCursor);
    }

private:
    QRectF closeRect() const
    {
        return QRectF(width() - 28, 4, 24, 24);
    }

    // Area where the thumbnail is drawn, keeping the page's aspect ratio.
    QRectF imageRect() const
    {
        if (m_thumbnail.isNull())
            return QRectF();

        const QRectF area = QRectF(rect()).adjusted(4, 4, -4, -4);

        QSizeF size(m_thumbnail.size());
        size.scale(area.size(), Qt::KeepAspectRatio);

        QRectF result(QPointF(), size);
        result.moveCenter(area.center());
        return result;
    }

    QPointF normalized(const QPointF &point) const
    {
        const QRectF image = imageRect();
        if (image.isEmpty())
            return QPointF();

        return QPointF(
            (point.x() - image.left()) / image.width(),
            (point.y() - image.top()) / image.height()
        );
    }

    static QPointF clamped(const QPointF &point)
    {
        return QPointF(
            qBound(0.0, point.x(), 1.0),
            qBound(0.0, point.y(), 1.0)
        );
    }

    bool m_expanded = false;
    bool m_pressed = false;
    bool m_dragging = false;

    QPixmap m_thumbnail;
    QRectF m_viewport;
    QPointF m_grabOffset;
};


// A named counter. Its pins take its color, and it is chosen in the toolbar.
struct CounterDef {
    int id = -1;
    QString name;
    QColor color;
};

// Shows one rendered page. It draws the page's QImage directly, so the
// pixels are kept once instead of being copied into a QPixmap.
class PageImageItem : public QGraphicsItem
{
public:
    void setImage(const QImage &image)
    {
        prepareGeometryChange();
        m_image = image;
        update();
    }

    const QImage &image() const
    {
        return m_image;
    }

    QRectF boundingRect() const override
    {
        return QRectF(0, 0, m_image.width(), m_image.height());
    }

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *,
               QWidget *) override
    {
        painter->drawImage(0, 0, m_image);
    }

private:
    QImage m_image;
};

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow()
    {
        setWindowTitle("CanvasPDF");
        // The window icon keeps its own colors, so it is not tinted.
        const QIcon windowIcon = QIcon::fromTheme("application-pdf");
        setWindowIcon(windowIcon.isNull()
            ? style()->standardIcon(QStyle::SP_FileIcon)
            : windowIcon);
        resize(1250, 850);

        scene = new QGraphicsScene(this);
        canvas = new PdfCanvas(scene, this);

        pageItem = new PageImageItem;
        scene->addItem(pageItem);

        // The start page and the PDF canvas share the central area.
        homePage = new HomePage(this);
        stack = new QStackedWidget(this);
        stack->addWidget(homePage);
        stack->addWidget(canvas);
        setCentralWidget(stack);

        connect(homePage, &HomePage::openRequested,
                this, &MainWindow::openPath);

        connect(homePage, &HomePage::openDialogRequested,
                this, &MainWindow::choosePdf);

        showHome();

        auto *toolbar = addToolBar("PDF Controls");
        toolbar->setObjectName("mainToolbar");
        toolbar->setMovable(false);
        toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

        QStyle *appStyle = style();

        QAction *homeAction = toolbar->addAction(
            themeIcon("go-home",
                      appStyle->standardIcon(QStyle::SP_DirHomeIcon)),
            "Home"
        );

        QAction *openAction = toolbar->addAction(
            themeIcon("document-open",
                      appStyle->standardIcon(QStyle::SP_DialogOpenButton)),
            "Open PDF"
        );
        QAction *previousAction = toolbar->addAction(
            themeIcon("go-previous",
                      appStyle->standardIcon(QStyle::SP_ArrowBack)),
            "Previous"
        );
        QAction *nextAction = toolbar->addAction(
            themeIcon("go-next",
                      appStyle->standardIcon(QStyle::SP_ArrowForward)),
            "Next"
        );
        // Zoom actions live in the status bar, so they are not toolbar actions.
        auto *fitAction = new QAction(
            themeIcon("zoom-fit-best"),
            "Fit Page",
            this
        );
        auto *zoomOutAction = new QAction(
            themeIcon("zoom-out"),
            "−",
            this
        );
        auto *zoomInAction = new QAction(
            themeIcon("zoom-in"),
            "+",
            this
        );
        pencilAction = toolbar->addAction(
            themeIcon("draw-freehand",
                      themeIcon("document-edit")),
            "Pencil (N)"
        );

        pencilAction->setCheckable(true);

        countAction = toolbar->addAction(
            themeIcon("mark-location",
                  appStyle->standardIcon(QStyle::SP_DialogApplyButton)),
            "Count"
        );
        countAction->setCheckable(true);
        countAction->setEnabled(false);
        countAction->setToolTip("Place numbered pins on the PDF (C)");

        connect(countAction, &QAction::triggered,
            this, &MainWindow::toggleCountMode);

        counterCombo = new QComboBox(toolbar);
        counterCombo->setToolTip("Counter that new pins belong to (1-9)");
        counterCombo->setEnabled(false);
        toolbar->addWidget(counterCombo);

        addCounterAction = toolbar->addAction(
            themeIcon("list-add",
                      appStyle->standardIcon(QStyle::SP_FileDialogNewFolder)),
            "New Counter"
        );
        addCounterAction->setToolTip("Add a counter");
        connect(addCounterAction, &QAction::triggered,
                this, [this] { addCounter(); });

        renameCounterAction = toolbar->addAction(
            themeIcon("document-edit",
                      appStyle->standardIcon(QStyle::SP_FileDialogDetailedView)),
            "Rename Counter"
        );
        renameCounterAction->setEnabled(false);
        renameCounterAction->setToolTip("Rename the active counter");
        connect(renameCounterAction, &QAction::triggered,
                this, &MainWindow::renameActiveCounter);

        removeCounterAction = toolbar->addAction(
            themeIcon("list-remove",
                      appStyle->standardIcon(QStyle::SP_TrashIcon)),
            "Remove Counter"
        );
        removeCounterAction->setEnabled(false);
        removeCounterAction->setToolTip("Remove the active counter and its pins");
        connect(removeCounterAction, &QAction::triggered,
                this, &MainWindow::removeActiveCounter);

        connect(counterCombo, &QComboBox::activated,
                this, &MainWindow::selectCounterAt);

        clearMarkersAction = toolbar->addAction(
            themeIcon("edit-clear",
                      appStyle->standardIcon(QStyle::SP_DialogResetButton)),
            "Clear Markers"
        );
        clearMarkersAction->setEnabled(false);
        clearMarkersAction->setToolTip("Remove all placed pins");
        connect(clearMarkersAction, &QAction::triggered, this, [this] {
            const int pins = canvas->pinCount();
            if (pins > 0) {
                const auto answer = QMessageBox::question(
                    this,
                    "Clear pins",
                    QString("Remove all %1 pins on every counter?").arg(pins)
                );
                if (answer != QMessageBox::Yes)
                    return;
            }

            canvas->clearCountMarkers();
            navMarker = nullptr;
        });

        connect(canvas, &PdfCanvas::countChanged,
            this, [this] { refreshCountLabel(); });

        pageNumber = new QSpinBox(this);
        pageNumber->setMinimum(1);
        pageNumber->setMaximum(1);
        pageNumber->setPrefix("Page ");
        pageNumber->setEnabled(false);
        toolbar->addWidget(pageNumber);

        auto *toolbarSpacer = new QWidget(toolbar);
        toolbarSpacer->setSizePolicy(QSizePolicy::Expanding,
                         QSizePolicy::Preferred);
        toolbar->addWidget(toolbarSpacer);

        countTotalLabel = new QLabel("No counter", toolbar);
        countTotalLabel->setObjectName("countTotal");
        toolbar->addWidget(countTotalLabel);

        zoomLabel = new QLabel("100%", this);

        auto *zoomControls = new QWidget(this);
        auto *zoomLayout = new QHBoxLayout(zoomControls);
        zoomLayout->setContentsMargins(0, 0, 0, 0);
        zoomLayout->setSpacing(4);

        auto addZoomButton = [this, zoomLayout](QAction *action) {
            auto *button = new QToolButton(this);
            button->setDefaultAction(action);
            button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
            zoomLayout->addWidget(button);
        };

        addZoomButton(fitAction);
        addZoomButton(zoomOutAction);
        zoomLayout->addWidget(zoomLabel);
        addZoomButton(zoomInAction);

        // Permanent widgets sit at the right end of the status bar.
        statusBar()->addPermanentWidget(zoomControls);

        // Overview pinned to the bottom-right corner of the canvas. It is
        // a child of the canvas so it floats over the page, and it is
        // hidden until a PDF is open.
        miniMap = new MiniMap(canvas);
        miniMap->hide();

        connect(miniMap, &MiniMap::toggled,
                this, &MainWindow::toggleMiniMap);

        connect(miniMap, &MiniMap::panRequested,
                this, &MainWindow::panToNormalized);

        connect(canvas, &PdfCanvas::viewportResized,
                this, &MainWindow::positionMiniMap);

        connect(canvas->horizontalScrollBar(), &QScrollBar::valueChanged,
                this, &MainWindow::updateMiniMapViewport);

        connect(canvas->verticalScrollBar(), &QScrollBar::valueChanged,
                this, &MainWindow::updateMiniMapViewport);

        connect(canvas, &PdfCanvas::zoomChanged,
                this, &MainWindow::updateMiniMapViewport);

        statusBar()->showMessage("Open a PDF to begin.");

        renderTimer.setSingleShot(true);
        renderTimer.setInterval(120);

        qRegisterMetaType<QList<PageLink>>();

        worker = new PdfWorker;
        worker->moveToThread(&renderThread);

        connect(this, &MainWindow::openRequested,
                worker, &PdfWorker::openDocument);

        connect(this, &MainWindow::renderRequested,
                worker, &PdfWorker::renderPage);

        connect(worker, &PdfWorker::documentOpened,
                this, &MainWindow::onDocumentOpened);

        connect(worker, &PdfWorker::pageRendered,
                this, &MainWindow::onPageRendered);

        connect(worker, &PdfWorker::errorOccurred,
                this, &MainWindow::onError);

        connect(worker, &PdfWorker::openFailed,
                this, &MainWindow::onOpenFailed);

        connect(this, &MainWindow::linksRequested,
                worker, &PdfWorker::loadLinks);

        connect(worker, &PdfWorker::linksLoaded,
                this, &MainWindow::onLinksLoaded);

        connect(canvas, &PdfCanvas::linkClicked,
                this, &MainWindow::onLinkClicked);

        connect(canvas, &PdfCanvas::hovered,
                this, &MainWindow::onCanvasHovered);

        qApp->installEventFilter(this);

        connect(&renderTimer, &QTimer::timeout,
                this, &MainWindow::requestRender);

        connect(openAction, &QAction::triggered,
                this, &MainWindow::choosePdf);

        connect(homeAction, &QAction::triggered,
                this, &MainWindow::showHome);

        connect(previousAction, &QAction::triggered,
                this, &MainWindow::previousPage);

        connect(nextAction, &QAction::triggered,
                this, &MainWindow::nextPage);

        connect(fitAction, &QAction::triggered,
                this, &MainWindow::fitPage);

        connect(zoomInAction, &QAction::triggered, this, [this] {
            canvas->scale(1.25, 1.25);
            canvas->zoomChanged(canvas->zoomLevel());
        });

        connect(zoomOutAction, &QAction::triggered, this, [this] {
            canvas->scale(0.8, 0.8);
            canvas->zoomChanged(canvas->zoomLevel());
        });

        connect(pencilAction, &QAction::triggered,
                this, [this](bool checked) {
            canvas->setPencilMode(checked);
        });

        connect(canvas, &PdfCanvas::pencilModeChanged,
                this, [this](bool enabled) {
            pencilAction->setChecked(enabled);

            statusBar()->showMessage(
                enabled
                    ? "Pencil mode: drag with the left mouse button to draw."
                    : "Pan mode: drag with the left mouse button to move."
            );
        });

        connect(canvas, &PdfCanvas::countModeChanged,
                this, [this](bool enabled) {
            countAction->setChecked(enabled);
            if (enabled)
                pencilAction->setChecked(false);
            statusBar()->showMessage(
                enabled
                    ? "Count mode: click the PDF to place the next numbered tick."
                    : "Pan mode: drag with the left mouse button to move."
            );
        });

        connect(canvas, &PdfCanvas::zoomChanged,
                this, [this](double zoom) {
            zoomLabel->setText(
                QString("%1%").arg(qRound(zoom * 100))
            );

            // Restart only when idle, so a long zoom still renders every
            // 120 ms and always renders its final level.
            if (!renderTimer.isActive())
                renderTimer.start();
        });

        connect(pageNumber, &QSpinBox::valueChanged,
                this, [this](int page) {
            if (pageCount > 0 && page != currentPage + 1) {
                currentPage = page - 1;
                canvas->setCurrentPage(currentPage);
                fitOnNextRender = true;
                renderTimer.start(0);
            }
        });

        // Keyboard shortcuts work even when a toolbar control has focus.
        auto addShortcut = [this](Qt::Key key, auto callback) {
            auto *shortcut = new QShortcut(
                QKeySequence(key),
                this
            );

            shortcut->setContext(Qt::ApplicationShortcut);
            connect(shortcut, &QShortcut::activated, this, callback);
        };

        addShortcut(Qt::Key_F, [this] {
            nextPage();
        });

        addShortcut(Qt::Key_D, [this] {
            previousPage();
        });

        addShortcut(Qt::Key_N, [this] {
            canvas->setPencilMode(!canvas->pencilMode());
        });

        addShortcut(Qt::Key_Escape, [this] {
            if (canvas->countMode())
                canvas->setCountMode(false);
            else if (canvas->pencilMode())
                canvas->setPencilMode(false);
        });

        addShortcut(Qt::Key_C, [this] {
            if (countAction->isEnabled())
                toggleCountMode(!canvas->countMode());
        });

        // 1-9 choose a counter while pins are being placed. They are ignored
        // while a number box or text field has focus, so typing still works.
        for (int i = 0; i < 9; ++i) {
            addShortcut(static_cast<Qt::Key>(Qt::Key_1 + i), [this, i] {
                if (canvas->countMode() && !textInputHasFocus())
                    selectCounterAt(i);
            });
        }

        addShortcut(Qt::Key_BracketRight, [this] { stepCounter(1); });
        addShortcut(Qt::Key_BracketLeft, [this] { stepCounter(-1); });

        auto *undo = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Z), this);
        undo->setContext(Qt::ApplicationShortcut);
        connect(undo, &QShortcut::activated, this, &MainWindow::undoStroke);

        // Redo: Ctrl+Y, and Ctrl+Shift+Z as the common alternative.
        for (const QKeySequence &sequence : {
                 QKeySequence(Qt::CTRL | Qt::Key_Y),
                 QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z)}) {
            auto *redo = new QShortcut(sequence, this);
            redo->setContext(Qt::ApplicationShortcut);
            connect(redo, &QShortcut::activated, this, &MainWindow::redoStroke);
        }

        renderThread.start();

        loadCounters();
    }

    ~MainWindow() override
    {
        qApp->removeEventFilter(this);
        renderTimer.stop();

        if (renderThread.isRunning()) {
            QMetaObject::invokeMethod(
                worker,
                "closeDocument",
                Qt::BlockingQueuedConnection
            );

            renderThread.quit();
            renderThread.wait();
        }

        // The render thread has stopped, so the worker can be freed here.
        // Its destructor closes the document and its context.
        delete worker;
        worker = nullptr;
    }

    void openFromCommandLine(const QString &path)
    {
        openPath(path);
    }

signals:
    void openRequested(const QString &path, quint64 openId);

    void renderRequested(
        int page,
        double scale,
        quint64 requestId,
        bool preview
    );

    void linksRequested(int page, quint64 requestId);

private slots:
    void choosePdf()
    {
        const QString path = QFileDialog::getOpenFileName(
            this,
            "Open PDF",
            QString(),
            "PDF documents (*.pdf)"
        );

        if (!path.isEmpty())
            openPath(path);
    }

    void openPath(const QString &path)
    {
        // Ignore repeat requests for a file already loading, such as a
        // double click on a recent entry.
        if (path == pendingOpenPath)
            return;

        pendingOpenPath = path;
        openId = ++lastOpenId;
        worker->latestOpen.store(openId);

        // Renders still queued for the previous document become stale.
        ++requestId;
        worker->latestRequest.store(requestId);

        renderTimer.stop();
        pageCount = 0;
        currentPage = 0;
        fitOnNextRender = true;
        thumbnailPage = -1;

        countAction->setEnabled(false);
        canvas->setCountMode(false);
        canvas->setPencilMode(false);
        canvas->clearCountMarkers();
        navMarker = nullptr;
        canvas->clearAnnotations();
        hasPendingPan = false;
        linkPage = -1;
        pageLinks.clear();
        ++linkRequestId;
        pageItem->setImage(QImage());
        shownPage = -1;
        shownScale = 0.0;
        fullImage = QImage();
        fullPage = -1;
        canvas->resetTransform();
        scene->setSceneRect(QRectF());
        updateMiniMapThumbnail();

        pageNumber->setEnabled(false);

        statusBar()->showMessage("Opening PDF...");
        emit openRequested(path, openId);
    }

    void onDocumentOpened(int count, const QString &path, quint64 id)
    {
        // Results from superseded opens are ignored.
        if (id != openId)
            return;

        pendingOpenPath.clear();
        rememberRecentFile(QFileInfo(path).absoluteFilePath());
        stack->setCurrentWidget(canvas);

        pageCount = count;
        currentPage = 0;

        pageNumber->blockSignals(true);
        pageNumber->setRange(1, pageCount);
        pageNumber->setValue(1);
        pageNumber->blockSignals(false);
        pageNumber->setEnabled(true);

        setWindowTitle(
            "CanvasPDF - " + QFileInfo(path).fileName()
        );

        canvas->setCurrentPage(currentPage);

        statusBar()->showMessage(
            QString("Opened %1 pages").arg(pageCount)
        );

        fitOnNextRender = true;
        renderTimer.start(0);
    }

    void requestRender()
    {
        if (pageCount <= 0)
            return;

        const quint64 id = ++requestId;
        worker->latestRequest.store(id);

        // The previous zoom level is stale now: stop rendering it.
        worker->cancelRender();

        const double zoom = canvas->zoomLevel();
        const double dpr = devicePixelRatioF();
        const double fullScale = fixedRenderScale();
        const bool aboveFit = zoom >= fitZoom() * 0.999;

        if (!fitOnNextRender && aboveFit && fullPage == currentPage) {
            // At fit or beyond: show the kept full-size bitmap at once.
            if (shownPage != currentPage
                || !qFuzzyCompare(shownScale, fullScale)) {
                showPageImage(fullImage, fullImageScale,
                              fullPixelX, fullPixelY);
                shownPage = currentPage;
                shownScale = fullScale;
            }

            updateLinksForCurrentPage();
            return;
        }

        if (!fitOnNextRender && !aboveFit) {
            // Below fit a smaller bitmap is enough. Keep one that is sharp.
            const double neededScale = qBound(0.15, zoom * dpr, 8.0);
            const double targetScale =
                qMin(neededScale * kZoomHeadroom, fullScale);
            const double sharpMinimum = qMin(neededScale, fullScale);

            const bool sharpEnough =
                shownPage == currentPage
                && shownScale > 0
                && shownScale >= sharpMinimum * 0.999
                && shownScale <= targetScale * kMaxOversample;

            if (sharpEnough) {
                updateLinksForCurrentPage();
                return;
            }

            requestedScale = targetScale;
        } else {
            // At fit or beyond, with no kept bitmap yet: render full size.
            requestedScale = fullScale;
        }

        // A quick preview is only useful when nothing sharp is on screen.
        const bool wantPreview = shownPage != currentPage;

        emit renderRequested(
            currentPage,
            requestedScale,
            id,
            wantPreview
        );

        updateLinksForCurrentPage();
    }

    // Device pixels per page point of the full-size render.
    double fixedRenderScale() const
    {
        return qBound(0.15, devicePixelRatioF() * kFixedZoom, 8.0);
    }

    // The zoom at which the whole page fits the window.
    double fitZoom()
    {
        const QSizeF page =
            pageItem->mapRectToScene(pageItem->boundingRect()).size();
        const QSizeF view = canvas->viewport()->size();

        if (page.isEmpty())
            return 0.0;

        return qMin(view.width() / page.width(),
                    view.height() / page.height());
    }

    // Puts a rendered page on screen at the scale it was rendered at.
    void showPageImage(const QImage &image, double renderScale,
                       int pixelX, int pixelY)
    {
        pageItem->setImage(image);

        QTransform imageTransform;
        imageTransform.scale(1.0 / renderScale, 1.0 / renderScale);

        pageItem->setTransform(imageTransform);
        pageItem->setPos(pixelX / renderScale, pixelY / renderScale);

        scene->setSceneRect(pageItem->sceneBoundingRect());
    }

    // Links depend only on the page, so they are read once per page.
    void updateLinksForCurrentPage()
    {
        if (currentPage != linkPage) {
            linkPage = currentPage;
            pageLinks.clear();
            emit linksRequested(currentPage, ++linkRequestId);
        }
    }

    void onLinksLoaded(
        const QList<PageLink> &links,
        int page,
        quint64 id
    )
    {
        if (id != linkRequestId || page != currentPage)
            return;

        pageLinks = links;
    }

    void onPageRendered(
        const QImage &image,
        int page,
        quint64 id,
        double renderScale,
        int pixelX,
        int pixelY,
        bool preview
    )
    {
        if (id != requestId || page != currentPage)
            return;

        // A preview would replace a sharp render of this page with a blurrier
        // one, so it is ignored once the page is already shown.
        if (preview && shownPage == page && shownScale > 0)
            return;

        showPageImage(image, renderScale, pixelX, pixelY);
        shownPage = page;
        // The requested scale is what the render aims for; a preview is
        // never considered sharp.
        shownScale = preview ? 0.0 : requestedScale;

        // Keep the full-size bitmap, so zooming back above fit is instant.
        if (!preview && qFuzzyCompare(requestedScale, fixedRenderScale())) {
            fullImage = image;
            fullPage = page;
            fullImageScale = renderScale;
            fullPixelX = pixelX;
            fullPixelY = pixelY;
        }

        countAction->setEnabled(true);
        clearMarkersAction->setEnabled(true);

        // The preview only shows the page while the full render is pending.
        if (preview)
            return;

        if (fitOnNextRender) {
            fitOnNextRender = false;

            canvas->fitInView(
                pageItem,
                Qt::KeepAspectRatio
            );

            // Render again at the actual fit scale for better clarity.
            renderTimer.start(0);
        }

        zoomLabel->setText(
            QString("%1%").arg(qRound(canvas->zoomLevel() * 100))
        );

        statusBar()->showMessage(
            QString("Page %1 of %2")
                .arg(currentPage + 1)
                .arg(pageCount)
        );

        // Same for a link that jumps to a point on its target page.
        if (hasPendingPan && !fitOnNextRender) {
            hasPendingPan = false;

            if (pendingPinFocus) {
                pendingPinFocus = false;
                focusPin(pendingPanCenter);
            } else {
                panToNormalized(pendingPanCenter);
            }
        }

        // The thumbnail only depends on the page, not the zoom level.
        if (page != thumbnailPage)
            updateMiniMapThumbnail();
        else
            updateMiniMapViewport();
    }

    void updateMiniMapThumbnail()
    {
        const QImage &page = pageItem->image();

        QPixmap thumbnail;
        if (!page.isNull()) {
            thumbnailPage = currentPage;
            thumbnail = QPixmap::fromImage(page.scaled(
                480,
                480,
                Qt::KeepAspectRatio,
                Qt::SmoothTransformation
            ));
        }

        miniMap->setThumbnail(thumbnail);
        miniMap->setVisible(!thumbnail.isNull());
        positionMiniMap();
        updateMiniMapViewport();
    }

    // Marks the visible part of the page on both minimaps.
    void updateMiniMapViewport()
    {
        const QRectF page = pageItem->sceneBoundingRect();

        if (pageItem->image().isNull() || page.isEmpty()) {
            miniMap->setViewport(QRectF());
            return;
        }

        const QRectF visible =
            canvas->mapToScene(canvas->viewport()->rect()).boundingRect();

        const QRectF viewport(
            (visible.left() - page.left()) / page.width(),
            (visible.top() - page.top()) / page.height(),
            visible.width() / page.width(),
            visible.height() / page.height()
        );

        miniMap->setViewport(viewport);
    }

    // Centers the canvas on a point given in page-normalized coordinates.
    void panToNormalized(const QPointF &center)
    {
        const QRectF page = pageItem->sceneBoundingRect();
        if (page.isEmpty())
            return;

        canvas->centerOn(
            page.left() + center.x() * page.width(),
            page.top() + center.y() * page.height()
        );
    }

    // Returns the link under a scene position, or null if there is none.
    const PageLink *linkAt(const QPointF &scenePos) const
    {
        if (pageLinks.isEmpty() || pageItem->image().isNull())
            return nullptr;

        const QRectF page = pageItem->sceneBoundingRect();
        if (page.isEmpty() || !page.contains(scenePos))
            return nullptr;

        const QPointF point(
            (scenePos.x() - page.left()) / page.width(),
            (scenePos.y() - page.top()) / page.height()
        );

        for (const PageLink &link : std::as_const(pageLinks)) {
            if (link.rect.contains(point))
                return &link;
        }
        return nullptr;
    }

    // Follows a link if the click landed on one.
    void onLinkClicked(const QPointF &scenePos)
    {
        const PageLink *link = linkAt(scenePos);
        if (!link)
            return;

        if (!link->uri.isEmpty())
            openExternalLink(link->uri);
        else if (link->targetPage >= 0)
            followInternalLink(*link);
    }

    // Pointing hand over a link while Ctrl is held.
    void onCanvasHovered(const QPointF &scenePos)
    {
        lastHoverPos = scenePos;
        if (canvas->countMode()) {
            canvas->updateCountPreview(scenePos);
            return;
        }
        updateLinkCursor(
            QGuiApplication::keyboardModifiers().testFlag(Qt::ControlModifier)
        );
    }

    void updateLinkCursor(bool ctrlDown)
    {
        canvas->setLinkCursor(ctrlDown && linkAt(lastHoverPos) != nullptr);
    }

    // Ctrl can be pressed or released without the mouse moving, so the
    // cursor is also updated from key events.
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::KeyPress
                || event->type() == QEvent::KeyRelease) {
            auto *key = static_cast<QKeyEvent *>(event);
            if (key->key() == Qt::Key_Control && !key->isAutoRepeat())
                updateLinkCursor(event->type() == QEvent::KeyPress);
        }
        return QMainWindow::eventFilter(watched, event);
    }

    void followInternalLink(const PageLink &link)
    {
        // Setting the same page number emits no change, so pan directly.
        if (link.targetPage == currentPage) {
            if (link.hasTarget)
                panToNormalized(link.target);
            return;
        }

        pendingPanCenter = link.target;
        hasPendingPan = link.hasTarget;
        pageNumber->setValue(link.targetPage + 1);
    }

    void openExternalLink(const QString &uri)
    {
        const QUrl url(uri, QUrl::TolerantMode);
        const QString scheme = url.scheme().toLower();

        // PDFs come from untrusted sources, so other schemes (file:, custom
        // app handlers) are not opened.
        if (scheme == "http" || scheme == "https" || scheme == "mailto") {
            // Confirm first: the link came from the document, not the user.
            const auto answer = QMessageBox::question(
                this,
                "Open link?",
                "This PDF wants to open:\n\n" + url.toDisplayString()
                    + "\n\nOpen it in your browser?",
                QMessageBox::Open | QMessageBox::Cancel,
                QMessageBox::Cancel
            );

            if (answer == QMessageBox::Open)
                QDesktopServices::openUrl(url);
        } else {
            statusBar()->showMessage(
                "Blocked link with unsupported scheme: " + scheme,
                5000
            );
        }
    }

    // Asks for a new name for the active counter. Pins keep their color.
    void renameActiveCounter()
    {
        const CounterDef *active = findCounter(activeCounterId);
        if (!active)
            return;

        const int id = active->id;
        bool ok = false;
        const QString name = QInputDialog::getText(
            this,
            "Rename counter",
            "Name:",
            QLineEdit::Normal,
            active->name,
            &ok
        ).trimmed();

        if (!ok || name.isEmpty())
            return;

        for (CounterDef &counter : counters) {
            if (counter.id == id)
                counter.name = name;
        }

        rebuildCounterCombo();
        refreshCountLabel();
        saveCounters();
        statusBar()->showMessage(QString("Renamed the counter to %1.").arg(name));
    }

    // Counters (name and color) are kept between runs. Their pins belong to
    // the document, so they are not kept.
    void saveCounters()
    {
        QSettings settings;
        settings.beginWriteArray("counters", counters.size());
        for (int i = 0; i < counters.size(); ++i) {
            settings.setArrayIndex(i);
            settings.setValue("name", counters.at(i).name);
            settings.setValue("color", counters.at(i).color.name());
        }
        settings.endArray();
    }

    void loadCounters()
    {
        QSettings settings;
        const int count = settings.beginReadArray("counters");
        for (int i = 0; i < count; ++i) {
            settings.setArrayIndex(i);

            CounterDef counter;
            counter.id = nextCounterId++;
            counter.name = settings.value("name").toString();
            if (counter.name.isEmpty())
                counter.name = QString("Counter %1").arg(counter.id);
            counter.color = QColor(settings.value("color").toString());
            if (!counter.color.isValid())
                counter.color = counterColor(i);

            counters.append(counter);
        }
        settings.endArray();

        if (!counters.isEmpty())
            selectCounter(counters.first().id);
        else
            rebuildCounterCombo();
    }

    // Turns pin placement on or off. The first counter is made on demand.
    void toggleCountMode(bool on)
    {
        if (on && counters.isEmpty())
            addCounter();

        canvas->setCountMode(on);
    }

    static QColor counterColor(int index)
    {
        static const QStringList palette = {
            "#bd5638", "#2e7d32", "#1565c0", "#6a1b9a", "#ad8b00", "#00838f"
        };
        return QColor(palette.at(index % palette.size()));
    }

    const CounterDef *findCounter(int id) const
    {
        for (const CounterDef &counter : counters) {
            if (counter.id == id)
                return &counter;
        }
        return nullptr;
    }

    void addCounter()
    {
        CounterDef counter;
        counter.id = nextCounterId++;
        counter.name = QString("Counter %1").arg(counter.id);
        counter.color = counterColor(counters.size());
        counters.append(counter);

        selectCounter(counter.id);
        saveCounters();
        statusBar()->showMessage(QString("Added %1.").arg(counter.name));
    }

    void selectCounter(int id)
    {
        const CounterDef *counter = findCounter(id);
        activeCounterId = counter ? id : -1;
        navMarker = nullptr;

        canvas->setActiveCounter(activeCounterId,
                                 counter ? counter->color : QColor());
        rebuildCounterCombo();
        refreshCountLabel();
    }

    // Chooses a counter by its position in the list (0 is the first).
    void selectCounterAt(int position)
    {
        if (position >= 0 && position < counters.size())
            selectCounter(counters.at(position).id);
    }

    void removeActiveCounter()
    {
        const CounterDef *active = findCounter(activeCounterId);
        if (!active)
            return;

        const int id = active->id;
        const QString name = active->name;
        const int pins = canvas->countTotal(id);

        if (pins > 0) {
            const auto answer = QMessageBox::question(
                this,
                "Remove counter",
                QString("Remove %1 and its %2 pins?").arg(name).arg(pins)
            );
            if (answer != QMessageBox::Yes)
                return;
        }

        canvas->removeCounterMarkers(id);

        for (int i = 0; i < counters.size(); ++i) {
            if (counters.at(i).id == id) {
                counters.removeAt(i);
                break;
            }
        }

        selectCounter(counters.isEmpty() ? -1 : counters.first().id);
        saveCounters();
        statusBar()->showMessage(QString("Removed %1.").arg(name));
    }

    // Fills the dropdown with the counters, each with its color.
    void rebuildCounterCombo()
    {
        counterCombo->blockSignals(true);
        counterCombo->clear();

        int index = -1;
        for (int i = 0; i < counters.size(); ++i) {
            QPixmap swatch(12, 12);
            swatch.fill(counters.at(i).color);
            counterCombo->addItem(QIcon(swatch), counters.at(i).name);

            if (counters.at(i).id == activeCounterId)
                index = i;
        }

        counterCombo->setCurrentIndex(index);
        counterCombo->setEnabled(!counters.isEmpty());
        removeCounterAction->setEnabled(!counters.isEmpty());
        renameCounterAction->setEnabled(!counters.isEmpty());
        counterCombo->blockSignals(false);
    }

    // Shows the active counter's pins on this page and in the whole document.
    void refreshCountLabel()
    {
        const CounterDef *active = findCounter(activeCounterId);
        if (!active) {
            countTotalLabel->setText("No counter");
            return;
        }

        countTotalLabel->setText(
            QString("%1: this page %2, all pages %3")
                .arg(active->name)
                .arg(canvas->countOn(currentPage, active->id))
                .arg(canvas->countTotal(active->id))
        );
    }

    // Moves to the next (direction > 0) or previous pin of the active
    // counter on the current page, wrapping at the ends.
    void stepCounter(int direction)
    {
        const QList<CountMarkerItem *> marks =
            canvas->markersOn(currentPage, activeCounterId);
        if (marks.isEmpty()) {
            statusBar()->showMessage("No pins for this counter on this page.");
            return;
        }

        int index = -1;
        for (int i = 0; i < marks.size(); ++i) {
            if (marks.at(i) == navMarker) {
                index = i;
                break;
            }
        }

        // Only a pin found on this page is safe to touch: an undone pin may
        // have been freed already, and there may be none yet.
        if (index >= 0)
            navMarker->setHighlighted(false);

        // Starting from no pin: next is the first, previous is the last.
        if (index < 0)
            index = direction > 0 ? -1 : marks.size();

        index = (index + direction + marks.size()) % marks.size();

        CountMarkerItem *marker = marks.at(index);
        navMarker = marker;
        marker->setHighlighted(true);

        focusPin(marker->fraction());
        statusBar()->showMessage(
            QString("Pin %1 of %2 on this page")
                .arg(index + 1).arg(marks.size())
        );
    }

    // Shows a pin: zooms in on it and centers the view, changing page if
    // needed.
    void jumpToPin(int page, const QPointF &fraction)
    {
        if (page == currentPage) {
            focusPin(fraction);
            return;
        }

        pendingPanCenter = fraction;
        hasPendingPan = true;
        pendingPinFocus = true;
        pageNumber->setValue(page + 1);
    }

    // Zooms to at least kPinZoom, then centers on a point of the page.
    void focusPin(const QPointF &fraction)
    {
        const double zoom = canvas->zoomLevel();
        if (zoom < kPinZoom) {
            canvas->scale(kPinZoom / zoom, kPinZoom / zoom);
            canvas->zoomChanged(canvas->zoomLevel());
        }

        panToNormalized(fraction);
    }

    bool textInputHasFocus() const
    {
        const QWidget *focus = QApplication::focusWidget();
        return qobject_cast<const QAbstractSpinBox *>(focus)
            || qobject_cast<const QLineEdit *>(focus);
    }

    void redoStroke()
    {
        if (canvas->redoLastStroke())
            statusBar()->showMessage("Restored the last undone line.");
        else
            statusBar()->showMessage("Nothing to redo.");
    }

    void undoStroke()
    {
        // While placing pins, undo removes the last pin. Otherwise it undoes
        // the last pencil line.
        if (canvas->countMode()) {
            if (canvas->undoLastMarker())
                statusBar()->showMessage("Removed the last pin.");
            else
                statusBar()->showMessage("Nothing to undo.");
            return;
        }

        if (canvas->undoLastStroke())
            statusBar()->showMessage("Removed the last line.");
        else
            statusBar()->showMessage("Nothing to undo.");
    }

    void showHome()
    {
        homePage->setRecentFiles(loadRecentFiles());
        stack->setCurrentWidget(homePage);
    }

    static QStringList loadRecentFiles()
    {
        QSettings settings;
        return settings.value("recentFiles").toStringList();
    }

    // Moves the path to the front of the recent list, keeping ten entries.
    static void rememberRecentFile(const QString &path)
    {
        constexpr int maxRecentFiles = 10;

        QStringList files = loadRecentFiles();
        files.removeAll(path);
        files.prepend(path);

        while (files.size() > maxRecentFiles)
            files.removeLast();

        QSettings settings;
        settings.setValue("recentFiles", files);
    }

    void toggleMiniMap()
    {
        miniMap->setExpanded(!miniMap->isExpanded());
        positionMiniMap();
        updateMiniMapViewport();
    }

    // Keeps the mini map anchored to the canvas's bottom-right corner.
    void positionMiniMap()
    {
        const int margin = 12;
        miniMap->move(
            canvas->width() - miniMap->width() - margin,
            canvas->height() - miniMap->height() - margin
        );
        miniMap->raise();
    }

    void onOpenFailed(const QString &message, quint64 id)
    {
        if (id != openId)
            return;

        pendingOpenPath.clear();
        onError(message);
    }

    void onError(const QString &message)
    {
        statusBar()->showMessage(message);
        QMessageBox::warning(this, "CanvasPDF", message);
    }

    void previousPage()
    {
        if (currentPage > 0)
            pageNumber->setValue(currentPage);
    }

    void nextPage()
    {
        if (currentPage + 1 < pageCount)
            pageNumber->setValue(currentPage + 2);
    }

    void fitPage()
    {
        if (pageCount <= 0 || pageItem->image().isNull())
            return;

        canvas->fitInView(pageItem, Qt::KeepAspectRatio);
        canvas->zoomChanged(canvas->zoomLevel());
    }

private:
    QGraphicsScene *scene = nullptr;
    PdfCanvas *canvas = nullptr;
    PageImageItem *pageItem = nullptr;

    QStackedWidget *stack = nullptr;
    HomePage *homePage = nullptr;

    // Links on the current page, and the page they were read for.
    QList<PageLink> pageLinks;
    int linkPage = -1;
    quint64 linkRequestId = 0;
    QPointF pendingPanCenter;
    bool hasPendingPan = false;
    // The pending pan is a pin being stepped to, so zoom in on it too.
    bool pendingPinFocus = false;
    QPointF lastHoverPos;

    MiniMap *miniMap = nullptr;

    QSpinBox *pageNumber = nullptr;
    QLabel *zoomLabel = nullptr;
    QLabel *countTotalLabel = nullptr;
    QAction *pencilAction = nullptr;
    QAction *countAction = nullptr;
    QAction *clearMarkersAction = nullptr;
    QComboBox *counterCombo = nullptr;
    QAction *addCounterAction = nullptr;
    QAction *removeCounterAction = nullptr;
    QAction *renameCounterAction = nullptr;

    QList<CounterDef> counters;
    int nextCounterId = 1;
    int activeCounterId = -1;
    // The pin the last [ or ] step went to, so the next step continues on.
    CountMarkerItem *navMarker = nullptr;

    QThread renderThread;
    PdfWorker *worker = nullptr;
    QTimer renderTimer;

    int pageCount = 0;
    int currentPage = 0;
    quint64 requestId = 0;
    // The page on screen, and the scale its render was asked for (0 while
    // only a preview is shown). requestedScale is the latest request's scale.
    int shownPage = -1;
    double shownScale = 0.0;
    double requestedScale = 0.0;
    // The full-size bitmap of one page, kept for zooming back above fit.
    QImage fullImage;
    int fullPage = -1;
    double fullImageScale = 0.0;
    int fullPixelX = 0;
    int fullPixelY = 0;
    quint64 lastOpenId = 0;
    quint64 openId = 0;
    QString pendingOpenPath;
    bool fitOnNextRender = true;
    int thumbnailPage = -1;
};


int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    applyDarkTheme(app);

    // QSettings uses these names to store the recent-files list.
    QCoreApplication::setOrganizationName("CanvasPDF");
    QCoreApplication::setApplicationName("CanvasPDF");

    MainWindow window;
    window.show();

    const QStringList arguments = QCoreApplication::arguments();
    if (arguments.size() > 1)
        window.openFromCommandLine(arguments.at(1));

    return app.exec();
}

#include "main.moc"
