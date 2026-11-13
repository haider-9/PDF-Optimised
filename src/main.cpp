
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
#include <QFrame>
#include <QPen>
#include <QBrush>
#include <QStyle>

#include <mupdf/fitz.h>

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

} // namespace

class PdfWorker : public QObject
{
    Q_OBJECT

public:
    std::atomic<quint64> latestRequest{0};

    ~PdfWorker() override
    {
        closeDocument();
    }

public slots:
    void openDocument(const QString &path)
    {
        closeDocument();

        const QByteArray encoded = path.toUtf8();

        ctx = fz_new_context(nullptr, nullptr, 256 * 1024 * 1024);

        if (!ctx) {
            emit errorOccurred("Could not create the MuPDF context.");
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
            emit errorOccurred("Could not open PDF: " + message);
            return;
        }

        if (pageCount < 1) {
            closeDocument();
            emit errorOccurred("The PDF contains no pages.");
            return;
        }

        emit documentOpened(pageCount, path);
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

    void renderPage(int pageIndex, double requestedScale, quint64 id)
    {
        if (id != latestRequest.load() || !ctx || !doc)
            return;

        if (pageIndex < 0 || pageIndex >= pageCount)
            return;

        fz_page *page = nullptr;
        fz_pixmap *pix = nullptr;

        int pixX = 0;
        int pixY = 0;

        double scale = qBound(0.15, requestedScale, 8.0);
        QString failure;

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

            pix = fz_new_pixmap_from_page(
                ctx,
                page,
                fz_scale(scale, scale),
                fz_device_rgb(ctx),
                0
            );

            pixX = fz_pixmap_x(ctx, pix);
            pixY = fz_pixmap_y(ctx, pix);
        }
        fz_catch(ctx) {
            failure = QString::fromUtf8(fz_caught_message(ctx));
        }

        if (failure.isEmpty() && pix &&
            id == latestRequest.load()) {

            const int width = fz_pixmap_width(ctx, pix);
            const int height = fz_pixmap_height(ctx, pix);
            const int stride = fz_pixmap_stride(ctx, pix);
            const int components = fz_pixmap_components(ctx, pix);

            if (components == 3) {
                QImage image(
                    fz_pixmap_samples(ctx, pix),
                    width,
                    height,
                    stride,
                    QImage::Format_RGB888
                );

                emit pageRendered(
                    image.copy(),
                    pageIndex,
                    id,
                    scale,
                    pixX,
                    pixY
                );
            } else {
                failure = "Unexpected PDF pixel format.";
            }
        }

        if (pix)
            fz_drop_pixmap(ctx, pix);

        if (page)
            fz_drop_page(ctx, page);

        if (!failure.isEmpty() && id == latestRequest.load())
            emit errorOccurred("Rendering failed: " + failure);
    }

signals:
    void documentOpened(int pageCount, const QString &path);

    void pageRendered(
        const QImage &image,
        int pageIndex,
        quint64 requestId,
        double renderScale,
        int pixelX,
        int pixelY
    );

    void errorOccurred(const QString &message);

private:
    fz_context *ctx = nullptr;
    fz_document *doc = nullptr;
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
        m_currentPathItem = nullptr;
        m_drawing = false;
    }

signals:
    void zoomChanged(double zoom);
    void pencilModeChanged(bool enabled);
    void viewportResized();

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

            m_currentPath = QPainterPath(
                mapToScene(event->position().toPoint())
            );

            QPen pen(
                QColor("#ff6b35"),
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
            m_annotations[m_currentPage].append(m_currentPathItem);

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
            m_currentPathItem = nullptr;
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

        setCentralWidget(canvas);

        auto *toolbar = addToolBar("PDF Controls");
        toolbar->setMovable(false);
        toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

        QStyle *appStyle = style();

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
        renderTimer.setInterval(180);

        worker = new PdfWorker;
        worker->moveToThread(&renderThread);

        connect(&renderThread, &QThread::finished,
                worker, &QObject::deleteLater);

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

        connect(&renderTimer, &QTimer::timeout,
                this, &MainWindow::requestRender);

        connect(openAction, &QAction::triggered,
                this, &MainWindow::choosePdf);

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
            if (canvas->pencilMode())
                canvas->setPencilMode(false);
        });

        renderThread.start();
    }

    ~MainWindow() override
    {
        renderTimer.stop();

        if (worker && renderThread.isRunning()) {
            QMetaObject::invokeMethod(
                worker,
                "closeDocument",
                Qt::BlockingQueuedConnection
            );

            renderThread.quit();
            renderThread.wait();
        }
    }

    void openFromCommandLine(const QString &path)
    {
        openPath(path);
    }

signals:
    void openRequested(const QString &path);

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
        ++requestId;
        worker->latestRequest.store(requestId);

        renderTimer.stop();
        pageCount = 0;
        currentPage = 0;
        fitOnNextRender = true;

        canvas->setPencilMode(false);
        canvas->clearAnnotations();
        pageItem->setPixmap(QPixmap());
        canvas->resetTransform();
        scene->setSceneRect(QRectF());
        updateMiniMapThumbnail();

        pageNumber->setEnabled(false);

        statusBar()->showMessage("Opening PDF...");
        emit openRequested(path);
    }

    void onDocumentOpened(int count, const QString &path)
    {
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
        int pixelY
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

        updateMiniMapThumbnail();
    }

    void updateMiniMapThumbnail()
    {
        const QPixmap page = pageItem->pixmap();

        QPixmap thumbnail;
        if (!page.isNull()) {
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
    bool fitOnNextRender = true;
};


int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    MainWindow window;
    window.show();

    const QStringList arguments = QCoreApplication::arguments();
    if (arguments.size() > 1)
        window.openFromCommandLine(arguments.at(1));

    return app.exec();
}

#include "main.moc"
