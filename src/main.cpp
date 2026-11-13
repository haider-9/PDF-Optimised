
#include <QApplication>
#include <QMainWindow>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
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
#include <QFont>
#include <QFrame>
#include <QPen>
#include <QBrush>
#include <QStyle>

#include <mupdf/fitz.h>

#include <QDockWidget>
#include <QScrollArea>
#include <QButtonGroup>
#include <QSignalBlocker>
#include <QMessageBox>

#include <algorithm>
#include <atomic>
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

// Pencil line counters. Each stroke stores the id of its counter.
constexpr int kCounterKey = 1;     // stroke data: counter id (-1 = none)
constexpr int kBaseWidthKey = 2;   // stroke data: pen width before highlight
constexpr qreal kMinStrokePixels = 10.0;  // shorter pencil strokes are discarded

struct CounterDef {
    int id = -1;
    QString name;
    QColor color;
};

QColor defaultPenColor()
{
    return QColor("#ff6b35");
}

// Colors handed out to new counters, cycling after the last one.
QColor counterPaletteColor(int index)
{
    static const QColor palette[] = {
        QColor("#ff6b35"), QColor("#2a9d8f"), QColor("#e9c46a"),
        QColor("#457b9d"), QColor("#9b5de5"), QColor("#06d6a0"),
        QColor("#f15bb5"), QColor("#00bbf9"), QColor("#fee440"),
        QColor("#8ac926"), QColor("#ff595e"), QColor("#6a4c93")
    };
    return palette[index % 12];
}

} // namespace

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

    void renderPage(int pageIndex, double requestedScale, quint64 id)
    {
        if (id != latestRequest.load() || !ctx || !doc)
            return;

        if (pageIndex < 0 || pageIndex >= pageCount)
            return;

        m_cookie.abort = 0;

        // Progressive render: a quarter-size preview appears quickly, then
        // the full-resolution render replaces it. Each pass checks whether
        // a newer request has arrived, so stale work stops early.
        const double finalScale = qBound(0.15, requestedScale, 8.0);
        renderPass(pageIndex, finalScale * 0.5, id, true);

        if (id != latestRequest.load() || m_cookie.abort != 0)
            return;

        renderPass(pageIndex, finalScale, id, false);
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
        volatile bool ok = false;

        fz_try(ctx) {
            page = fz_load_page(ctx, doc, pageIndex);

            const fz_rect bounds = fz_bound_page(ctx, page);
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

            device = fz_new_draw_device(ctx, fz_identity, pix);
            fz_run_page(
                ctx,
                page,
                device,
                fz_concat(ctm, fz_translate(float(-box.x0), float(-box.y0))),
                &m_cookie
            );
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

private:
    fz_context *ctx = nullptr;
    fz_document *doc = nullptr;
    fz_cookie m_cookie{};
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

    void setPencilMode(bool enabled)
    {
        m_pencilMode = enabled;
        setDragMode(
            enabled
                ? QGraphicsView::NoDrag
                : QGraphicsView::ScrollHandDrag
        );

        if (enabled)
            setCursor(Qt::CrossCursor);
        else
            unsetCursor();

        emit pencilModeChanged(enabled);
    }

    void setCurrentPage(int page)
    {
        m_currentPage = page;

        for (auto it = m_annotations.begin();
             it != m_annotations.end(); ++it) {
            for (auto *item : it.value())
                item->setVisible(it.key() == page);
        }
    }

    // Sets the counter and color used for strokes drawn from now on.
    // A counter of -1 means the stroke is not counted.
    void setPenCounter(int counter, const QColor &color)
    {
        m_penCounter = counter;
        m_penColor = color;
    }

    // Every stroke in the document, in page order.
    QList<QPair<int, QGraphicsPathItem *>> allStrokes() const
    {
        QList<int> pages = m_annotations.keys();
        std::sort(pages.begin(), pages.end());

        QList<QPair<int, QGraphicsPathItem *>> result;
        for (int page : pages) {
            for (QGraphicsPathItem *item : m_annotations.value(page))
                result.append({page, item});
        }

        return result;
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

        emit strokesChanged();
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

        emit strokesChanged();
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

    // Stops counting the strokes of a removed counter; they stay drawn.
    void untagCounter(int id)
    {
        for (const auto &entry : allStrokes()) {
            if (entry.second->data(kCounterKey).toInt() == id)
                entry.second->setData(kCounterKey, -1);
        }
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
    void viewportResized();
    void strokesChanged();

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
            m_currentPathItem->setData(kCounterKey, m_penCounter);
            m_currentPathItem->setData(kBaseWidthKey, pen.widthF());
            m_annotations[m_currentPage].append(m_currentPathItem);
            m_strokeOrder.append({m_currentPage, m_currentPathItem});

            event->accept();
            return;
        }

        if (event->button() == Qt::MiddleButton) {
            middlePanning = true;
            lastMousePosition = event->position().toPoint();
            setCursor(Qt::ClosedHandCursor);
            event->accept();
            return;
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

        QGraphicsView::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && m_drawing) {
            m_drawing = false;
            if (!isLongEnough(m_currentPathItem))
                discardLastStroke();
            m_currentPathItem = nullptr;
            emit strokesChanged();
            event->accept();
            return;
        }

        if (event->button() == Qt::MiddleButton && middlePanning) {
            middlePanning = false;

            if (m_pencilMode)
                setCursor(Qt::CrossCursor);
            else
                unsetCursor();

            event->accept();
            return;
        }

        QGraphicsView::mouseReleaseEvent(event);
    }

private:
    bool m_pencilMode = false;
    bool m_drawing = false;
    bool middlePanning = false;

    int m_currentPage = 0;
    QPoint lastMousePosition;

    QPainterPath m_currentPath;
    QGraphicsPathItem *m_currentPathItem = nullptr;

    QHash<int, QList<QGraphicsPathItem *>> m_annotations;

    // Strokes in the order they were drawn, for undo.
    QList<QPair<int, QGraphicsPathItem *>> m_strokeOrder;

    // Undone strokes, kept off the scene so they can be redone.
    QList<QPair<int, QGraphicsPathItem *>> m_redoStack;

    int m_penCounter = -1;
    QColor m_penColor = defaultPenColor();
    QGraphicsPathItem *m_highlighted = nullptr;
};


// Sidebar listing the pencil line counters, one row per counter.
class CounterPanel : public QWidget
{
    Q_OBJECT

public:
    struct Row {
        int id = -1;
        int index = 0;          // position on the sidebar, 0-based
        QString name;
        QColor color;
        int onPage = 0;
        int total = 0;
        int position = 0;       // 1-based number of the focused line, 0 if none
    };

    explicit CounterPanel(QWidget *parent = nullptr) : QWidget(parent)
    {
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(8, 8, 8, 8);
        layout->setSpacing(8);

        auto *header = new QHBoxLayout;
        auto *title = new QLabel("Counters", this);
        QFont titleFont = title->font();
        titleFont.setBold(true);
        title->setFont(titleFont);
        header->addWidget(title, 1);

        m_removeButton = new QToolButton(this);
        m_removeButton->setText("−");
        m_removeButton->setToolTip(
            "Remove the active counter (its lines stay but are not counted)"
        );
        connect(m_removeButton, &QToolButton::clicked,
                this, &CounterPanel::removeRequested);
        header->addWidget(m_removeButton);

        auto *addButton = new QToolButton(this);
        addButton->setText("+");
        addButton->setToolTip("Add a counter (C)");
        connect(addButton, &QToolButton::clicked,
                this, &CounterPanel::addRequested);
        header->addWidget(addButton);

        layout->addLayout(header);

        // Scope: count and step through all lines, or only this page's.
        auto *scope = new QHBoxLayout;
        m_documentButton = new QPushButton("Whole document", this);
        m_pageButton = new QPushButton("This page", this);
        for (QPushButton *button : {m_documentButton, m_pageButton}) {
            button->setCheckable(true);
            button->setCursor(Qt::PointingHandCursor);
            scope->addWidget(button);
        }
        m_documentButton->setChecked(true);

        auto *group = new QButtonGroup(this);
        group->addButton(m_documentButton);
        group->addButton(m_pageButton);
        group->setExclusive(true);

        connect(m_pageButton, &QPushButton::toggled,
                this, &CounterPanel::scopeChanged);

        layout->addLayout(scope);

        m_container = new QWidget(this);
        m_rows = new QVBoxLayout(m_container);
        m_rows->setContentsMargins(0, 0, 0, 0);
        m_rows->setSpacing(10);
        m_rows->addStretch();

        auto *scroll = new QScrollArea(this);
        scroll->setWidget(m_container);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        layout->addWidget(scroll, 1);

        // Shortcut legend: each key in its own box, aligned with its action.
        const QList<QPair<QString, QString>> shortcuts = {
            {"N", "Pencil on / off"},
            {"1 – 9", "Select counter by sidebar position"},
            {"[  ]", "Previous / next line of the active counter"},
            {"C", "Add a counter"},
            {"Ctrl+Z", "Undo last line"},
            {"Ctrl+Y", "Redo last line"},
            {"Esc", "Leave pencil mode"},
        };

        QString html = "<div style='margin-top: 6px;'><b>Shortcuts</b></div>"
                       "<table cellspacing='4'>";
        for (const auto &shortcut : shortcuts) {
            html += QString(
                "<tr><td style='white-space: nowrap;'>"
                "<code style='border: 1px solid gray; border-radius: 3px;"
                " padding: 0 4px;'>%1</code></td>"
                "<td>%2</td></tr>"
            ).arg(shortcut.first.toHtmlEscaped(), shortcut.second.toHtmlEscaped());
        }
        html += "</table>";

        auto *legend = new QLabel(html, this);
        legend->setTextFormat(Qt::RichText);
        legend->setWordWrap(true);
        layout->addWidget(legend);
    }

    void setPageScoped(bool on)
    {
        QSignalBlocker blocker(m_pageButton);
        m_pageButton->setChecked(on);
        m_documentButton->setChecked(!on);
    }

    void setRows(const QList<Row> &rows, int activeId, bool pageScoped)
    {
        // Keep the trailing stretch; replace everything before it.
        while (m_rows->count() > 1) {
            QLayoutItem *item = m_rows->takeAt(0);
            if (QWidget *widget = item->widget())
                widget->deleteLater();
            delete item;
        }

        for (const Row &row : rows) {
            m_rows->insertWidget(
                m_rows->count() - 1,
                buildRow(row, row.id == activeId, pageScoped)
            );
        }

        m_removeButton->setEnabled(!rows.isEmpty());
    }

signals:
    void counterSelected(int counter);
    void stepRequested(int counter, int direction);
    void addRequested();
    void removeRequested();
    void scopeChanged(bool pageScoped);

private:
    QWidget *buildRow(const Row &row, bool active, bool pageScoped)
    {
        const int id = row.id;

        auto *box = new QWidget(m_container);
        auto *layout = new QVBoxLayout(box);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(4);

        const QColor text = qGray(row.color.rgb()) > 160
            ? QColor(Qt::black)
            : QColor(Qt::white);
        const QString border = active
            ? "3px solid palette(text)"
            : "3px solid transparent";

        auto *select = new QPushButton(box);
        select->setMinimumHeight(56);
        select->setCursor(Qt::PointingHandCursor);
        select->setStyleSheet(QString(
            "QPushButton { background: %1; color: %2; border: %3;"
            " border-radius: 8px; padding: 6px 10px; text-align: left; }"
        ).arg(row.color.name(), text.name(), border));
        const QString stats = pageScoped
            ? QString("%1 on this page").arg(row.onPage)
            : QString("%1 on this page · %2 total")
                  .arg(row.onPage)
                  .arg(row.total);
        select->setText(QString("%1  %2\n%3")
            .arg(row.index + 1)
            .arg(row.name)
            .arg(stats));
        connect(select, &QPushButton::clicked, this, [this, id] {
            emit counterSelected(id);
        });
        layout->addWidget(select);

        auto *prev = new QToolButton(box);
        prev->setText("‹");
        prev->setEnabled(row.total > 0);
        connect(prev, &QToolButton::clicked, this, [this, id] {
            emit stepRequested(id, -1);
        });

        auto *next = new QToolButton(box);
        next->setText("›");
        next->setEnabled(row.total > 0);
        connect(next, &QToolButton::clicked, this, [this, id] {
            emit stepRequested(id, 1);
        });

        auto *label = new QLabel(box);
        label->setAlignment(Qt::AlignCenter);
        label->setText(row.position > 0
            ? QString("%1 / %2").arg(row.position).arg(row.total)
            : QString("%1 lines").arg(row.total));

        auto *nav = new QHBoxLayout;
        nav->addWidget(prev);
        nav->addWidget(label, 1);
        nav->addWidget(next);
        layout->addLayout(nav);

        return box;
    }

    QWidget *m_container = nullptr;
    QVBoxLayout *m_rows = nullptr;
    QToolButton *m_removeButton = nullptr;
    QPushButton *m_documentButton = nullptr;
    QPushButton *m_pageButton = nullptr;
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
        QFont titleFont = title->font();
        titleFont.setPointSize(titleFont.pointSize() + 10);
        titleFont.setBold(true);
        title->setFont(titleFont);
        layout->addWidget(title);

        layout->addWidget(new QLabel("Recently opened", this));

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
        m_empty->setAlignment(Qt::AlignCenter);
        layout->addWidget(m_empty, 1);

        auto *openButton = new QPushButton(
            themeIcon("document-open",
                      style()->standardIcon(QStyle::SP_DialogOpenButton)),
            "Open PDF...",
            this
        );
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

        pageItem = scene->addPixmap(QPixmap());
        pageItem->setTransformationMode(Qt::SmoothTransformation);

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

        // Line counters are niche, so they are off unless enabled here.
        countersAction = toolbar->addAction(
            themeIcon("view-list-details",
                      appStyle->standardIcon(QStyle::SP_FileDialogDetailedView)),
            "Counters"
        );
        countersAction->setCheckable(true);
        countersAction->setToolTip("Count pencil lines per page");

        countersPanel = new CounterPanel(this);
        countersDock = new QDockWidget("Counters", this);
        countersDock->setFeatures(QDockWidget::NoDockWidgetFeatures);
        countersDock->setWidget(countersPanel);
        addDockWidget(Qt::RightDockWidgetArea, countersDock);
        countersDock->hide();

        connect(countersAction, &QAction::triggered,
                this, &MainWindow::setCountersEnabled);

        connect(countersPanel, &CounterPanel::counterSelected,
                this, &MainWindow::selectCounter);

        connect(countersPanel, &CounterPanel::stepRequested,
                this, &MainWindow::navigate);

        connect(countersPanel, &CounterPanel::addRequested,
                this, &MainWindow::addCounter);

        connect(countersPanel, &CounterPanel::scopeChanged,
                this, &MainWindow::setPageScoped);

        connect(countersPanel, &CounterPanel::removeRequested,
                this, &MainWindow::removeActiveCounter);

        connect(canvas, &PdfCanvas::strokesChanged,
                this, &MainWindow::refreshCounters);

        pageNumber = new QSpinBox(this);
        pageNumber->setMinimum(1);
        pageNumber->setMaximum(1);
        pageNumber->setPrefix("Page ");
        pageNumber->setEnabled(false);
        toolbar->addWidget(pageNumber);

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
                refreshCounters();
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
            if (canvas->pencilMode())
                canvas->setPencilMode(false);
        });

        // Keyboard shortcuts for counting.
        for (int i = 0; i < 9; ++i) {
            addShortcut(static_cast<Qt::Key>(Qt::Key_1 + i), [this, i] {
                if (countersEnabled)
                    selectCounterAt(i);
            });
        }

        addShortcut(Qt::Key_BracketRight, [this] {
            if (countersEnabled)
                navigate(activeCounterId, 1);
        });

        addShortcut(Qt::Key_BracketLeft, [this] {
            if (countersEnabled)
                navigate(activeCounterId, -1);
        });

        addShortcut(Qt::Key_C, [this] {
            if (countersEnabled)
                addCounter();
        });

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

        addCounter();

        pageScoped = QSettings().value("countersPageScoped", false).toBool();
        countersPanel->setPageScoped(pageScoped);

        const bool enabled =
            QSettings().value("countersEnabled", false).toBool();
        countersAction->setChecked(enabled);
        setCountersEnabled(enabled);
    }

    ~MainWindow() override
    {
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
        quint64 requestId
    );

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

        canvas->setPencilMode(false);
        canvas->clearAnnotations();
        navItem = nullptr;
        hasPendingFocus = false;
        pageItem->setPixmap(QPixmap());
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
        refreshCounters();
    }

    void requestRender()
    {
        if (pageCount <= 0)
            return;

        const quint64 id = ++requestId;
        worker->latestRequest.store(id);

        // The previous zoom level is stale now: stop rendering it.
        worker->cancelRender();

        const double dpr = devicePixelRatioF();
        const double renderScale = canvas->zoomLevel() * dpr;

        emit renderRequested(
            currentPage,
            qBound(0.15, renderScale, 8.0),
            id
        );
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

        pageItem->setPixmap(QPixmap::fromImage(image));

        QTransform imageTransform;
        imageTransform.scale(
            1.0 / renderScale,
            1.0 / renderScale
        );

        pageItem->setTransform(imageTransform);
        pageItem->setPos(
            pixelX / renderScale,
            pixelY / renderScale
        );

        scene->setSceneRect(pageItem->sceneBoundingRect());

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

        // Finish a line jump that waited for its page to render. Skip the
        // fit pass so the jump lands at the final zoom.
        if (hasPendingFocus && !fitOnNextRender) {
            hasPendingFocus = false;
            applyFocus(pendingFocusRect);
        }

        // The thumbnail only depends on the page, not the zoom level.
        if (page != thumbnailPage)
            updateMiniMapThumbnail();
        else
            updateMiniMapViewport();
    }

    void updateMiniMapThumbnail()
    {
        const QPixmap page = pageItem->pixmap();

        QPixmap thumbnail;
        if (!page.isNull()) {
            thumbnailPage = currentPage;
            thumbnail = page.scaled(
                480,
                480,
                Qt::KeepAspectRatio,
                Qt::SmoothTransformation
            );
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

        if (pageItem->pixmap().isNull() || page.isEmpty()) {
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

    const CounterDef *findCounter(int id) const
    {
        for (const CounterDef &counter : counters) {
            if (counter.id == id)
                return &counter;
        }
        return nullptr;
    }

    void setCountersEnabled(bool enabled)
    {
        countersEnabled = enabled;
        countersDock->setVisible(enabled);
        applyPenCounter();
        refreshCounters();
        QSettings().setValue("countersEnabled", enabled);
    }

    void applyPenCounter()
    {
        const CounterDef *active = findCounter(activeCounterId);

        if (countersEnabled && active)
            canvas->setPenCounter(active->id, active->color);
        else
            canvas->setPenCounter(-1, defaultPenColor());
    }

    void addCounter()
    {
        CounterDef counter;
        counter.id = nextCounterId++;
        counter.name = QString("Counter %1").arg(counter.id);
        counter.color = counterPaletteColor(static_cast<int>(counters.size()));
        counters.append(counter);

        activeCounterId = counter.id;
        applyPenCounter();
        refreshCounters();
        statusBar()->showMessage(QString("Added %1.").arg(counter.name));
    }

    void removeActiveCounter()
    {
        const CounterDef *active = findCounter(activeCounterId);
        if (!active)
            return;

        const int removedId = active->id;
        const QString removedName = active->name;

        int lines = 0;
        for (const auto &entry : canvas->allStrokes()) {
            if (entry.second->data(kCounterKey).toInt() == removedId)
                ++lines;
        }

        if (lines > 0) {
            const auto answer = QMessageBox::question(
                this,
                "Remove counter",
                QString("Remove %1? Its %2 lines stay on the page but are "
                        "no longer counted.").arg(removedName).arg(lines)
            );
            if (answer != QMessageBox::Yes)
                return;
        }

        canvas->untagCounter(removedId);

        for (int i = 0; i < counters.size(); ++i) {
            if (counters.at(i).id == removedId) {
                counters.removeAt(i);
                break;
            }
        }

        navItem = nullptr;
        activeCounterId = counters.isEmpty() ? -1 : counters.first().id;
        applyPenCounter();
        refreshCounters();
        statusBar()->showMessage(QString("Removed %1.").arg(removedName));
    }

    void selectCounter(int id)
    {
        if (!findCounter(id))
            return;

        activeCounterId = id;
        applyPenCounter();
        refreshCounters();
    }

    // Selects the counter at a sidebar position (0-based).
    void selectCounterAt(int position)
    {
        if (position < counters.size())
            selectCounter(counters.at(position).id);
    }

    // Lines of a counter in the current scope, in page order.
    QList<QPair<int, QGraphicsPathItem *>> linesFor(int id) const
    {
        QList<QPair<int, QGraphicsPathItem *>> lines;
        for (const auto &entry : canvas->allStrokes()) {
            if (entry.second->data(kCounterKey).toInt() != id)
                continue;
            if (pageScoped && entry.first != currentPage)
                continue;
            lines.append(entry);
        }
        return lines;
    }

    void setPageScoped(bool on)
    {
        pageScoped = on;
        QSettings().setValue("countersPageScoped", on);
        refreshCounters();
    }

    // Recomputes the sidebar numbers for the current scope.
    void refreshCounters()
    {
        QHash<int, QList<QGraphicsPathItem *>> lines;
        QHash<int, int> onPage;
        bool navAlive = false;

        for (const auto &entry : canvas->allStrokes()) {
            QGraphicsPathItem *stroke = entry.second;
            if (stroke == navItem)
                navAlive = true;

            const int id = stroke->data(kCounterKey).toInt();
            if (id < 0)
                continue;

            const bool here = entry.first == currentPage;
            if (here)
                ++onPage[id];
            if (!pageScoped || here)
                lines[id].append(stroke);
        }

        if (!navAlive)
            navItem = nullptr;

        QList<CounterPanel::Row> rows;
        for (int i = 0; i < counters.size(); ++i) {
            const CounterDef &counter = counters.at(i);
            const QList<QGraphicsPathItem *> &ids = lines[counter.id];

            CounterPanel::Row row;
            row.id = counter.id;
            row.index = i;
            row.name = counter.name;
            row.color = counter.color;
            row.onPage = onPage.value(counter.id);
            row.total = ids.size();

            const int index = ids.indexOf(navItem);
            row.position = index >= 0 ? index + 1 : 0;

            rows.append(row);
        }

        countersPanel->setRows(rows, activeCounterId, pageScoped);
    }

    // Steps to the next (direction > 0) or previous line of a counter,
    // across all pages, wrapping at the ends.
    void navigate(int id, int direction)
    {
        if (!findCounter(id))
            return;

        const CounterDef *counter = findCounter(id);

        const auto lines = linesFor(id);

        if (lines.isEmpty()) {
            statusBar()->showMessage(
                QString("%1 has no lines yet. Draw with the pencil.")
                    .arg(counter->name)
            );
            return;
        }

        int current = -1;
        for (int i = 0; i < lines.size(); ++i) {
            if (lines.at(i).second == navItem)
                current = i;
        }

        const int count = lines.size();
        const int next = direction > 0
            ? (current + 1) % count
            : (current <= 0 ? count - 1 : current - 1);

        navItem = lines.at(next).second;
        canvas->highlightStroke(navItem);

        const int page = lines.at(next).first;
        focusOnStroke(page, navItem->sceneBoundingRect());

        statusBar()->showMessage(
            QString("%1 · line %2 of %3 · page %4")
                .arg(counter->name)
                .arg(next + 1)
                .arg(count)
                .arg(page + 1)
        );

        refreshCounters();
    }

    // Shows a scene rectangle, switching pages first if needed.
    void focusOnStroke(int page, const QRectF &rect)
    {
        if (page == currentPage) {
            applyFocus(rect);
            return;
        }

        pendingFocusRect = rect;
        hasPendingFocus = true;
        pageNumber->setValue(page + 1);
    }

    // Zooms so the rectangle fills about a third of the view, centered.
    void applyFocus(const QRectF &rect)
    {
        const QSizeF view = canvas->viewport()->size();

        const qreal fitWidth =
            view.width() / qMax(rect.width() * 3.0, 60.0);
        const qreal fitHeight =
            view.height() / qMax(rect.height() * 3.0, 60.0);
        const qreal zoom = qBound(0.5, qMin(fitWidth, fitHeight), 4.0);

        canvas->resetTransform();
        canvas->scale(zoom, zoom);
        canvas->centerOn(rect.center());
        canvas->zoomChanged(canvas->zoomLevel());
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
        if (pageCount <= 0 || pageItem->pixmap().isNull())
            return;

        canvas->fitInView(pageItem, Qt::KeepAspectRatio);
        canvas->zoomChanged(canvas->zoomLevel());
    }

private:
    QGraphicsScene *scene = nullptr;
    PdfCanvas *canvas = nullptr;
    QGraphicsPixmapItem *pageItem = nullptr;

    QStackedWidget *stack = nullptr;
    HomePage *homePage = nullptr;

    CounterPanel *countersPanel = nullptr;
    QDockWidget *countersDock = nullptr;
    QAction *countersAction = nullptr;
    bool countersEnabled = false;
    bool pageScoped = false;
    QList<CounterDef> counters;
    int nextCounterId = 1;
    int activeCounterId = -1;
    QGraphicsPathItem *navItem = nullptr;
    QRectF pendingFocusRect;
    bool hasPendingFocus = false;

    MiniMap *miniMap = nullptr;

    QSpinBox *pageNumber = nullptr;
    QLabel *zoomLabel = nullptr;
    QAction *pencilAction = nullptr;

    QThread renderThread;
    PdfWorker *worker = nullptr;
    QTimer renderTimer;

    int pageCount = 0;
    int currentPage = 0;
    quint64 requestId = 0;
    quint64 lastOpenId = 0;
    quint64 openId = 0;
    QString pendingOpenPath;
    bool fitOnNextRender = true;
    int thumbnailPage = -1;
};


int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

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
