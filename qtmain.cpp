/* Qt front end for the radar map viewer.
 *
 * The map is still drawn by the original C code into a memory surface; this
 * file only shows that surface and turns clicks into the key codes the core
 * already understands (see key_pressed() in image.c).
 */

#include <QApplication>
#include <QMainWindow>
#include <QWidget>
#include <QPushButton>
#include <QToolButton>
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QTimer>
#include <QImage>
#include <QPainter>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QStatusBar>
#include <QShortcut>
#include <QEventLoop>
#include <QCloseEvent>
#include <QScrollBar>
#include <QScreen>
#include <QLayout>
#include <QResizeEvent>
#include <QComboBox>
#include <QCheckBox>
#include <QFileDialog>
#include <QPolygonF>

#include <clocale>
#include <cstring>

#include "qt_bridge.h"

/* key codes the core expects, from key_pressed() and archive.c */
enum {
    KEY_ENTER      = 13,
    KEY_ESC        = 27,
    KEY_HOME       = 327,
    KEY_ARROW_UP   = 328,
    KEY_ARROW_LEFT = 331,
    KEY_ARROW_RIGHT= 333,
    KEY_END        = 335,
    KEY_ARROW_DOWN = 336,
    KEY_F1         = 315,  /* geography overlays are F1..F8 */
    KEY_F2         = 316,  /* archive: mark animation start */
    KEY_F3         = 317   /* archive: mark animation end   */
};

/* The C side writes cp866, as the .wrk files and the config do - the map
 * labels, the legend rows and the product titles all come through in it.  The
 * only part of the page that matters here is the cyrillic, which sits in two
 * contiguous runs, so this is a formula rather than a table.  (Qt5 had
 * QTextCodec for the job; Qt6 moved it out of QtCore, and one font's worth of
 * legend labels is not worth a dependency on Qt5Compat.) */
static QString cp866(const char *text)
{
    QString out;

    if (text == nullptr) return out;
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        if      (*p < 0x80)                out += QChar(*p);
        else if (*p <= 0xAF)               out += QChar(0x410 + (*p - 0x80));
        else if (*p >= 0xE0 && *p <= 0xEF) out += QChar(0x440 + (*p - 0xE0));
        else if (*p == 0xF0)               out += QChar(0x401);   /* YO */
        else if (*p == 0xF1)               out += QChar(0x451);   /* yo */
        else                               out += QChar(' ');     /* frames */
    }
    return out;
}

/* QMouseEvent lost x()/y() in Qt6 and gained position(); Qt5 has pos() */
static inline QPoint eventPos(QMouseEvent *e)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return e->position().toPoint();
#else
    return e->pos();
#endif
}

/* ------------------------------------------------------------------ */
/* a fixed pane onto one region of the surface                         */
/* ------------------------------------------------------------------ */

/* The palette and the cursor readout are drawn by the C code into the
 * margins of the same surface as the map.  Showing those regions in panes
 * of their own keeps them on screen while the map scrolls underneath. */
class SurfaceStrip : public QWidget
{
public:
    SurfaceStrip(const QImage *surface, QWidget *parent = nullptr)
        : QWidget(parent), image(surface) {}

    void setRegion(const QRect &r)
    {
        region = r;
        /* Fixed width, but the height is a preference, not a demand: these
         * used to be setFixedSize(), which made the window taller than some
         * screens and left the compositor unable to maximise it. */
        setFixedWidth(r.width());
        setMinimumHeight(0);
        setMaximumHeight(r.height());
        updateGeometry();
        update();
    }

    QSize sizeHint() const override { return region.size(); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        if (image == nullptr || image->isNull() || region.isEmpty()) {
            painter.fillRect(rect(), Qt::black);
            return;
        }
        painter.drawImage(QPoint(0, 0), *image, region);
    }

private:
    const QImage *image;
    QRect         region;
};

/* ------------------------------------------------------------------ */
/* the canvas: shows the surface the C code renders into               */
/* ------------------------------------------------------------------ */

class MapCanvas : public QWidget
{
    Q_OBJECT

public:
    explicit MapCanvas(QWidget *parent = nullptr) : QWidget(parent)
    {
        setMouseTracking(true);          /* mouse_move() wants every motion */
        setFocusPolicy(Qt::StrongFocus); /* keep the original key bindings  */
        /* grow with the window: the map is resized to match, rather than
         * being a fixed picture with empty scroll area around it */
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setMinimumSize(320, 240);
        qApp->installEventFilter(this);  /* see eventFilter() below         */
        rebuildImage();
    }

    QSize sizeHint() const override { return mapRegion.size(); }

    /* Blocking key read for the archive browser.  The browser draws itself
     * on the screen surface and then asks for a key, so a nested event loop
     * keeps the window alive and repainting while it waits.  The filter on
     * the application makes every key reach it, whatever holds the focus. */
    int waitForKey()
    {
        pendingKey = -1;
        waiting = true;
        emit waitStateChanged(true);
        setFocus(Qt::OtherFocusReason);
        update();

        QEventLoop loop;
        keyLoop = &loop;
        loop.exec();
        keyLoop = nullptr;

        waiting = false;
        emit waitStateChanged(false);

        /* nothing supplied means the window went away: Escape unwinds it */
        return pendingKey < 0 ? KEY_ESC : pendingKey;
    }

    /* Non blocking key read, used between animation frames: shows the frame
     * just drawn and reports a key if one is pending, 0 otherwise. */
    int pollKey()
    {
        /* draw_map() is also called from timer() and from inside the event
         * handling below, so never pump events recursively: a nested poll
         * would eat the key the outer animate() is waiting for. */
        if (animating || suppressPoll) return 0;

        refresh();
        animating = true;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        animating = false;

        int key = polledKey;
        polledKey = 0;
        return key;
    }

    bool isWaiting() const { return waiting; }
    bool isAnimating() const { return animating; }

    /* repaint this widget and every pane showing another part of the
     * same surface */
    void refresh() { update(); emit surfaceChanged(); }

    const QImage *surface() const { return &image; }
    QRect mapArea() const { return mapRegion; }
    void setPolledKey(int code) { polledKey = code; }

    /* used by the buttons, and on close, to answer a waitForKey() */
    void injectKey(int code)
    {
        if (!waiting) return;
        pendingKey = code;
        if (keyLoop) keyLoop->quit();
    }

    /* Re-wrap both framebuffers; call once the core has created them. */
    void rebuildImage()
    {
        unsigned char *pixels = qt_screen_pixels();
        int w = qt_screen_width();
        int h = qt_screen_height();

        if (pixels && w > 0 && h > 0) {
            image = QImage(pixels, w, h, qt_screen_stride(),
                           formatFor(qt_screen_bpp()));
            /* this widget shows the map region only - the palette and the
             * readout are shown by their own panes, which do not scroll */
            mapRegion = QRect(qt_map_origin_x(), qt_map_origin_y(),
                              qt_map_width(), qt_map_height());
            mapRegion &= QRect(0, 0, w, h);
        }

        unsigned char *map = qt_map_pixels();
        int mw = qt_map_width();
        int mh = qt_map_height();
        if (map && mw > 0 && mh > 0) {
            mapImage = QImage(map, mw, mh, qt_map_stride(),
                              formatFor(qt_map_bpp()));
            mapOrigin = QPoint(qt_map_origin_x(), qt_map_origin_y());
        }
    }

    /* How GRX laid the pixels out.  It is chosen at run time: the memory
     * driver picks the mode, and a build elsewhere may not pick the same
     * one this machine does. */
    static QImage::Format formatFor(int bpp)
    {
        switch (bpp) {
        case 24: return QImage::Format_BGR888;  /* GRX RAM24: B,G,R bytes */
        case 32: return QImage::Format_RGB32;   /* 0x00RRGGBB little endian */
        default:
            fprintf(stderr, "unsupported GRX surface depth: %d bpp "
                            "(run with IMAGEQT_DEBUG=1 for details)\n", bpp);
            return QImage::Format_BGR888;
        }
    }

signals:
    void positionChanged(int x, int y);
    void quitRequested();
    void waitStateChanged(bool waiting);
    void surfaceChanged();
    void crossSectionChanged(int waitingForSecondPoint);
    void crossSectionReady();
    void dataChanged();
    void mapResized();

protected:
    /* The archive browser and the animation both read keys themselves, and
     * neither can rely on this widget holding the focus - a button may have
     * it.  Grab keys for them here; otherwise let them take the normal
     * route to keyPressEvent(). */
    bool eventFilter(QObject *object, QEvent *event) override
    {
        if (event->type() != QEvent::KeyPress)
            return QWidget::eventFilter(object, event);
        int code = translate(static_cast<QKeyEvent *>(event));
        if (code < 0)
            return QWidget::eventFilter(object, event);

        if (waiting)        injectKey(code);   /* archive browser  */
        else if (animating) polledKey = code;  /* animation loop   */
        else {                                 /* normal operation */
            if (key_pressed(code)) emit quitRequested();
            refresh();
            /* the file or the product may have changed under an open cross
             * section window; it decides for itself whether to follow */
            emit dataChanged();
        }
        return true;
    }

    void paintEvent(QPaintEvent *event) override
    {
        QPainter painter(this);
        if (image.isNull()) {
            painter.fillRect(rect(), Qt::black);
            return;
        }
        /* the core composes the map into this surface itself, so that the
         * archive window it draws afterwards is not hidden by it */
        painter.drawImage(event->rect(), image,
                          event->rect().translated(mapRegion.topLeft()));
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        /* the core works in surface coordinates, this widget starts at the
         * map's corner */
        int x = eventPos(event).x() + mapRegion.x();
        int y = eventPos(event).y() + mapRegion.y();
        mouse_move(x, y);
        emit positionChanged(x, y);
        refresh();
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        const int x = eventPos(event).x() + mapRegion.x();
        const int y = eventPos(event).y() + mapRegion.y();

        /* the cross section reads the cursor position from the globals
         * mouse_move() maintains, so make sure they match this click */
        mouse_move(x, y);

        if (event->button() == Qt::LeftButton) {
            const int before = cross_section_state();
            cross_section_click();
            const int after = cross_section_state();
            emit crossSectionChanged(after);
            refresh();
            /* 1 -> 0 is the second click: both ends of the cut are in */
            if (before == 1 && after == 0) emit crossSectionReady();
        } else if (event->button() == Qt::RightButton) {
            /* the core calls this one mouse_click_left, but the X11 loop
             * only ever fed it right button presses - keep that behaviour */
            mouse_click_left(x, y);
            refresh();
        }
        setFocus();
    }

    /* Give the core a map buffer the size of this widget and repaint into it.
     * draw_map() ends in qt_poll_key(), which pumps the event loop; doing that
     * from inside a resize re-enters this handler, so hold it off. */
    void resizeEvent(QResizeEvent *event) override
    {
        QWidget::resizeEvent(event);
        if (!qt_resize_map(width(), height())) return;
        rebuildImage();
        suppressPoll = true;
        draw_map(1);
        qt_redraw_readout();
        suppressPoll = false;
        emit mapResized();
        refresh();
    }

    /* keys are handled in eventFilter(), so that they work no matter which
     * widget holds the focus - the canvas never has to be clicked first */

private:
    /* Qt key -> the DOS style codes key_pressed() switches on */
    static int translate(QKeyEvent *event)
    {
        switch (event->key()) {
        case Qt::Key_Escape: return KEY_ESC;
        case Qt::Key_Left:   return KEY_ARROW_LEFT;
        case Qt::Key_Right:  return KEY_ARROW_RIGHT;
        case Qt::Key_Up:     return KEY_ARROW_UP;
        case Qt::Key_Down:   return KEY_ARROW_DOWN;
        case Qt::Key_Home:   return KEY_HOME;
        case Qt::Key_End:    return KEY_END;
        case Qt::Key_Return:
        case Qt::Key_Enter:  return KEY_ENTER;
        default: break;
        }
        if (event->key() >= Qt::Key_F1 && event->key() <= Qt::Key_F8)
            return KEY_F1 + (event->key() - Qt::Key_F1);

        const QString text = event->text();
        if (text.size() == 1 && text.at(0).unicode() < 128)
            return text.at(0).unicode();
        return -1;
    }

    QImage image;      /* the whole composed surface */
    QRect  mapRegion;  /* the part of it this widget shows */
    QImage mapImage;   /* the map alone, kept for debugging */
    QPoint mapOrigin;

    bool        waiting = false;   /* inside waitForKey() */
    bool        animating = false; /* inside pollKey()    */
    bool        suppressPoll = false; /* inside resizeEvent() */
    int         pendingKey = -1;
    int         polledKey = 0;
    QEventLoop *keyLoop = nullptr;
};

/* the canvas currently serving the core's input calls */
static MapCanvas *g_canvas = nullptr;

extern "C" int qt_wait_key(void)
{
    if (g_canvas == nullptr) return KEY_ESC;
    return g_canvas->waitForKey();
}

extern "C" int qt_poll_key(void)
{
    if (g_canvas == nullptr) return 0;
    return g_canvas->pollKey();
}

/* ------------------------------------------------------------------ */
/* the vertical cross section                                          */
/* ------------------------------------------------------------------ */

/* The plot itself.  crosssect.c hands over physical values on a regular grid -
 * distance along the cut across, altitude up - and this turns them into a
 * picture: the values through the product's palette, the ground the lowest
 * beam leaves unseen as a grey band under them, and axes in kilometres both
 * ways.  It owns the section, and frees it. */
class CrossSectionView : public QWidget
{
    Q_OBJECT

public:
    explicit CrossSectionView(QWidget *parent = nullptr) : QWidget(parent)
    {
        std::memset(&cs, 0, sizeof(cs));
        setMouseTracking(true);
        setMinimumSize(420, 280);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setAutoFillBackground(true);
        QPalette p = palette();
        p.setColor(QPalette::Window, QColor(24, 24, 28));
        setPalette(p);
    }

    ~CrossSectionView() override { cross_section_release(&cs); }

    /* Recompute from the line the map window marked.  false means there was
     * nothing to compute - too few levels in the file, or too short a cut. */
    bool build(int x1, int y1, int x2, int y2, int family, bool smooth)
    {
        unsigned char rgb[256*3];

        cross_section_release(&cs);
        image = QImage();

        if (!cross_section_compute(x1, y1, x2, y2, family, smooth ? 1 : 0, &cs)) {
            update();
            return false;
        }
        if (!cross_section_colors(family, rgb)) std::memset(rgb, 0, sizeof(rgb));

        /* row 0 of the section is the ground, row 0 of a QImage is the top */
        image = QImage(cs.width, cs.height, QImage::Format_ARGB32);
        image.fill(Qt::transparent);
        for (int iz = 0; iz < cs.height; iz++) {
            QRgb *row = (QRgb *)image.scanLine(cs.height - 1 - iz);
            const float *src = cs.value + (size_t)iz * cs.width;
            for (int ix = 0; ix < cs.width; ix++) {
                const int byte = cross_section_byte(cs.family, src[ix]);
                row[ix] = byte < 0 ? 0u
                        : qRgb(rgb[byte*3], rgb[byte*3+1], rgb[byte*3+2]);
            }
        }
        update();
        return true;
    }

    const struct cross_section *section() const
    {
        return cs.value != nullptr ? &cs : nullptr;
    }

    /* The plot, without the axes around it. */
    QRect plotRect() const
    {
        QRect r = rect().adjusted(LeftMargin, TopMargin, -RightMargin, -BottomMargin);
        return r.isValid() ? r : QRect();
    }

signals:
    void probed(const QString &text);

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        const QRect plot = plotRect();

        if (cs.value == nullptr || plot.isEmpty()) {
            painter.setPen(Qt::lightGray);
            painter.drawText(rect(), Qt::AlignCenter,
                             tr("No section: the file needs at least two\n"
                                "levels of this product, and a longer cut."));
            return;
        }

        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter.setRenderHint(QPainter::Antialiasing, true);

        painter.fillRect(plot, QColor(12, 12, 16));
        painter.drawImage(plot, image);

        drawGround(painter, plot);
        drawGrid(painter, plot);
        drawLevels(painter, plot);

        painter.setPen(QColor(150, 150, 160));
        painter.drawRect(plot.adjusted(0, 0, -1, -1));
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        const QRect plot = plotRect();
        const QPoint at = eventPos(event);

        if (cs.value == nullptr || !plot.contains(at)) { emit probed(QString()); return; }

        int ix = (at.x() - plot.left()) * cs.width / plot.width();
        int iz = (plot.bottom() - at.y()) * cs.height / plot.height();
        if (ix < 0) ix = 0;
        if (iz < 0) iz = 0;
        if (ix >= cs.width)  ix = cs.width - 1;
        if (iz >= cs.height) iz = cs.height - 1;

        const float km = ix * cs.length_km / (cs.width > 1 ? cs.width - 1 : 1);
        const float z  = iz * cs.top_km / (cs.height > 1 ? cs.height - 1 : 1);
        const float v  = cs.value[(size_t)iz * cs.width + ix];

        QString reading = v > -9000.0f
            ? QString("%1 %2").arg(v, 0, 'f', 1).arg(cp866(cross_section_units(cs.family)))
            : (z < cs.floor_km[ix] ? tr("under the lowest beam") : tr("no data"));

        emit probed(tr("%1 km along, %2 km up:  %3")
                    .arg(km, 0, 'f', 1).arg(z, 0, 'f', 2).arg(reading));
    }

    void leaveEvent(QEvent *) override { emit probed(QString()); }

private:
    enum { LeftMargin = 52, RightMargin = 14, TopMargin = 10, BottomMargin = 34 };

    /* Where the lowest beam passes overhead: nothing under this line was ever
     * scanned, which is not the same as nothing being there. */
    void drawGround(QPainter &painter, const QRect &plot) const
    {
        QPolygonF band;

        band << QPointF(plot.left(), plot.bottom());
        for (int ix = 0; ix < cs.width; ix++)
            band << QPointF(xOf(plot, ix), yOf(plot, cs.floor_km[ix]));
        band << QPointF(plot.right(), plot.bottom());

        /* a cut far enough from every radar has its whole column under the
         * lowest beam, and the band then runs off the top of the plot */
        painter.save();
        painter.setClipRect(plot);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QBrush(QColor(120, 120, 128, 150), Qt::BDiagPattern));
        painter.drawPolygon(band);
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor(170, 170, 180), 1));
        painter.drawPolyline(band.mid(1, cs.width));
        painter.restore();
    }

    /* Distance along the bottom, altitude up the side.  The horizontal step is
     * picked so that eight or so labels fit whatever the cut is long. */
    void drawGrid(QPainter &painter, const QRect &plot) const
    {
        static const float steps[] = { 1, 2, 5, 10, 20, 25, 50, 100, 200 };
        float step = steps[sizeof(steps)/sizeof(steps[0]) - 1];

        for (unsigned i = 0; i < sizeof(steps)/sizeof(steps[0]); i++)
            if (cs.length_km / steps[i] <= 8.0f) { step = steps[i]; break; }

        painter.setPen(QPen(QColor(90, 90, 100), 1, Qt::DotLine));
        for (float km = step; km < cs.length_km; km += step) {
            const int x = plot.left() + (int)(km / cs.length_km * plot.width());
            painter.drawLine(x, plot.top(), x, plot.bottom());
        }
        for (float km = 1.0f; km < cs.top_km; km += 1.0f)
            painter.drawLine(plot.left(), (int)yOf(plot, km),
                             plot.right(), (int)yOf(plot, km));

        painter.setPen(QColor(210, 210, 215));
        for (float km = 0; km <= cs.length_km + 0.01f; km += step) {
            const int x = plot.left() + (int)(km / cs.length_km * plot.width());
            painter.drawText(QRect(x - 30, plot.bottom() + 4, 60, 16),
                             Qt::AlignHCenter | Qt::AlignTop,
                             QString::number(km, 'f', 0));
        }
        for (float km = 0; km <= cs.top_km + 0.01f; km += 1.0f) {
            painter.drawText(QRect(0, (int)yOf(plot, km) - 8, LeftMargin - 6, 16),
                             Qt::AlignRight | Qt::AlignVCenter,
                             QString::number(km, 'f', 0));
        }
        painter.drawText(QRect(0, plot.bottom() + 4, LeftMargin - 6, 16),
                         Qt::AlignRight | Qt::AlignTop, tr("km"));
    }

    /* The altitudes the data actually sits at.  Everything between them is
     * interpolated, and it is worth being able to see which is which. */
    void drawLevels(QPainter &painter, const QRect &plot) const
    {
        painter.setPen(QPen(QColor(255, 255, 255, 90), 1, Qt::DashLine));
        for (int i = 0; i < cs.levels; i++) {
            const int y = (int)yOf(plot, cs.level_km[i]);
            painter.drawLine(plot.left(), y, plot.right(), y);
        }
    }

    double xOf(const QRect &plot, int ix) const
    {
        return plot.left() + (double)ix * plot.width() / (cs.width > 1 ? cs.width - 1 : 1);
    }

    double yOf(const QRect &plot, double km) const
    {
        return plot.bottom() - km / cs.top_km * plot.height();
    }

    struct cross_section cs;
    QImage image;
};

/* The legend column, from the same palette the plot is coloured with. */
class CrossSectionLegend : public QWidget
{
    Q_OBJECT

public:
    explicit CrossSectionLegend(QWidget *parent = nullptr) : QWidget(parent)
    {
        setFixedWidth(96);
    }

    void setFamily(int family)
    {
        unsigned char rgb[3];
        char label[32];

        rows.clear();
        for (int i = 0; cross_section_legend(family, i, rgb, label, sizeof(label)); i++)
            rows << qMakePair(QColor(rgb[0], rgb[1], rgb[2]), cp866(label));
        title = cp866(cross_section_title(family));
        units = cp866(cross_section_units(family));
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        int y = 4;

        painter.drawText(QRect(2, y, width() - 4, 16), Qt::AlignLeft, title);
        y += 16;
        painter.drawText(QRect(2, y, width() - 4, 16), Qt::AlignLeft, units);
        y += 20;

        for (int i = 0; i < rows.size(); i++) {
            painter.fillRect(QRect(2, y, 12, 11), rows[i].first);
            painter.setPen(Qt::gray);
            painter.drawRect(QRect(2, y, 12, 11));
            painter.setPen(palette().color(QPalette::WindowText));
            painter.drawText(QRect(20, y - 2, width() - 22, 15),
                             Qt::AlignLeft | Qt::AlignVCenter, rows[i].second);
            y += 14;
        }
    }

private:
    QList<QPair<QColor, QString> > rows;
    QString title, units;
};

/* The window.  It keeps the cut the map window marked and recomputes on
 * demand: another product family, interpolation off to compare against the
 * nearest sample, or a new file arriving under it. */
class CrossSectionWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit CrossSectionWindow(QWidget *parent = nullptr) : QMainWindow(parent)
    {
        view = new CrossSectionView;
        legend = new CrossSectionLegend;

        family = new QComboBox;
        connect(family, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this](int) { rebuild(); });

        smooth = new QCheckBox(tr("3D interpolation"));
        smooth->setChecked(true);
        smooth->setToolTip(tr("Off: the nearest level and the nearest grid cell, "
                              "which is what the imagegcc section draws"));
        connect(smooth, &QCheckBox::toggled, this, [this](bool) { rebuild(); });

        follow = new QCheckBox(tr("Follow the map"));
        follow->setChecked(true);
        follow->setToolTip(tr("Recut whenever the map window loads another file"));

        QPushButton *save = new QPushButton(tr("Save PNG"));
        connect(save, &QPushButton::clicked, this, &CrossSectionWindow::save);

        QWidget *bar = new QWidget;
        QHBoxLayout *barLayout = new QHBoxLayout(bar);
        barLayout->setContentsMargins(6, 4, 6, 0);
        barLayout->addWidget(new QLabel(tr("Product:")));
        barLayout->addWidget(family);
        barLayout->addWidget(smooth);
        barLayout->addWidget(follow);
        barLayout->addStretch(1);
        barLayout->addWidget(save);

        QWidget *plot = new QWidget;
        QHBoxLayout *plotLayout = new QHBoxLayout(plot);
        plotLayout->setContentsMargins(6, 6, 6, 6);
        plotLayout->addWidget(view, 1);
        plotLayout->addWidget(legend, 0);

        QWidget *central = new QWidget;
        QVBoxLayout *column = new QVBoxLayout(central);
        column->setContentsMargins(0, 0, 0, 0);
        column->addWidget(bar, 0);
        column->addWidget(plot, 1);
        setCentralWidget(central);

        connect(view, &CrossSectionView::probed, this, [this](const QString &text) {
            if (text.isEmpty()) statusBar()->showMessage(summary);
            else statusBar()->showMessage(text);
        });

        setWindowTitle(tr("Vertical cross section"));
        resize(900, 520);
    }

    /* Take a fresh cut.  The endpoints come from the map window, which has
     * just had its second click. */
    void recut()
    {
        int list[8];
        const int count = cross_section_families(list, 8);
        const int wanted = family->count() > 0 ? family->currentData().toInt() : -1;

        /* the families on offer depend on what the file carries, so rebuild
         * the list, keeping the one on screen selected where it survives */
        family->blockSignals(true);
        family->clear();
        for (int i = 0; i < count; i++) {
            family->addItem(nameOf(list[i]), list[i]);
            if (list[i] == wanted) family->setCurrentIndex(i);
        }
        family->blockSignals(false);

        rebuild();
    }

    /* The map window loaded another file: follow it, if that is wanted. */
    void dataChanged()
    {
        if (isVisible() && follow->isChecked()) recut();
    }

protected:
    /* Closing takes the marker off the map with it. */
    void closeEvent(QCloseEvent *event) override
    {
        cross_section_forget();
        emit closed();
        QMainWindow::closeEvent(event);
    }

signals:
    void closed();

private:
    static QString nameOf(int fam)
    {
        switch (fam) {
        case QT_FAM_DBZ: return tr("Reflectivity");
        case QT_FAM_ZDR: return tr("ZDR");
        case QT_FAM_VEL: return tr("Velocity");
        default:         return tr("Product %1").arg(fam);
        }
    }

    void rebuild()
    {
        int x1, y1, x2, y2;

        if (family->count() == 0) {
            summary = tr("This file carries no product with two levels to cut through.");
            statusBar()->showMessage(summary);
            view->build(0, 0, 0, 0, -1, false);
            return;
        }

        const int fam = family->currentData().toInt();
        cross_section_endpoints(&x1, &y1, &x2, &y2);
        legend->setFamily(fam);

        if (!view->build(x1, y1, x2, y2, fam, smooth->isChecked())) {
            summary = tr("Nothing to draw: %1 has %2 usable level(s) in this file.")
                      .arg(nameOf(fam)).arg(cross_section_levels(fam));
        } else {
            const struct cross_section *cs = view->section();
            summary = tr("%1 km long, %2 levels between %3 and %4 km, %5")
                      .arg(cs->length_km, 0, 'f', 1)
                      .arg(cs->levels)
                      .arg(cs->base_km, 0, 'f', 1)
                      .arg(cs->top_km, 0, 'f', 1)
                      .arg(cs->smooth ? tr("interpolated") : tr("nearest sample"));
        }
        statusBar()->showMessage(summary);
    }

    void save()
    {
        const QString path = QFileDialog::getSaveFileName(
            this, tr("Save the cross section"), "crosssection.png",
            tr("PNG image (*.png)"));

        if (path.isEmpty()) return;
        if (!grab().save(path))
            statusBar()->showMessage(tr("Could not write %1").arg(path), 5000);
    }

    CrossSectionView   *view;
    CrossSectionLegend *legend;
    QComboBox          *family;
    QCheckBox          *smooth;
    QCheckBox          *follow;
    QString             summary;
};

/* ------------------------------------------------------------------ */
/* the window: canvas plus the buttons                                 */
/* ------------------------------------------------------------------ */

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow()
    {
        canvas = new MapCanvas;
        g_canvas = canvas;

        mapScroll = new QScrollArea;
        mapScroll->setWidgetResizable(true);   /* the canvas follows the viewport */
        mapScroll->setWidget(canvas);
        mapScroll->setAlignment(Qt::AlignCenter);
        mapScroll->setBackgroundRole(QPalette::Dark);

        /* The palette and the cursor readout are parts of the same surface,
         * shown in panes of their own so that scrolling the map does not
         * carry them off screen. */
        int x, y, w, h;
        legend = new SurfaceStrip(canvas->surface());
        qt_legend_rect(&x, &y, &w, &h);
        legend->setRegion(QRect(x, y, w, h));

        readout = new SurfaceStrip(canvas->surface());
        qt_readout_rect(&x, &y, &w, &h);
        readout->setRegion(QRect(x, y, w, h));

        /* The buttons are what used to make the window 1069 pixels tall at
         * the very least - more than the work area on a good many screens,
         * and a window that cannot be made to fit cannot be maximised
         * either.  Let the column scroll instead of dictating a minimum. */
        QWidget *panel = buildPanel();
        QScrollArea *panelScroll = new QScrollArea;
        panelScroll->setWidget(panel);
        panelScroll->setWidgetResizable(true);
        panelScroll->setFrameShape(QFrame::NoFrame);
        panelScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        panelScroll->setFixedWidth(panel->sizeHint().width()
                                   + panelScroll->verticalScrollBar()->sizeHint().width());

        QWidget *central = new QWidget;
        QHBoxLayout *layout = new QHBoxLayout(central);
        layout->addWidget(panelScroll);
        layout->addWidget(legend,  0, Qt::AlignTop);
        layout->addWidget(mapScroll, 1);
        layout->addWidget(readout, 0, Qt::AlignTop);
        setCentralWidget(central);

        /* every pane draws from the one surface, so they repaint together */
        connect(canvas, &MapCanvas::surfaceChanged, legend,
                QOverload<>::of(&QWidget::update));
        connect(canvas, &MapCanvas::surfaceChanged, readout,
                QOverload<>::of(&QWidget::update));

        connect(canvas, &MapCanvas::mapResized, this, [this]() {
            /* qt_readout_rect() is WINDOW_LEFT+WINDOW_XSIZE, which just moved */
            int x, y, w, h;
            qt_readout_rect(&x, &y, &w, &h);
            readout->setRegion(QRect(x, y, w, h));
        });

        connect(canvas, &MapCanvas::quitRequested, this, &QWidget::close);
        connect(canvas, &MapCanvas::waitStateChanged, this,
                [this](bool waiting) {
                    /* the browser is drawn at the map's top left corner,
                     * which may be scrolled out of sight */
                    if (waiting) {
                        mapScroll->horizontalScrollBar()->setValue(0);
                        mapScroll->verticalScrollBar()->setValue(0);
                    }
                    /* make it obvious where the keys go now */
                    archiveKeys->setTitle(waiting ? tr("Archive keys - ACTIVE")
                                                  : tr("Archive keys"));
                    archiveKeys->setStyleSheet(
                        waiting ? "QGroupBox { font-weight: bold; }" : "");
                    if (waiting)
                        statusBar()->showMessage(
                            tr("Archive open - arrows move, Enter opens, Esc closes "
                               "(the Archive keys buttons do the same)"));
                    else
                        statusBar()->clearMessage();
                });
        connect(canvas, &MapCanvas::crossSectionChanged, this,
                [this](int waiting) {
                    statusBar()->showMessage(waiting
                        ? tr("Cross section: click the second point")
                        : tr("Left click picks two points for a vertical "
                             "cross section, which opens in its own window"),
                        waiting ? 0 : 4000);
                });
        connect(canvas, &MapCanvas::crossSectionReady, this,
                &MainWindow::showCrossSection);
        connect(canvas, &MapCanvas::dataChanged, this, [this]() {
            if (section) section->dataChanged();
        });
        connect(canvas, &MapCanvas::positionChanged, this,
                [this](int x, int y) {
                    statusBar()->showMessage(tr("x=%1  y=%2").arg(x).arg(y));
                });

        /* timer() polls for freshly arrived files, as the old loop did.  It
         * redraws the map, so it must stay out of the way while the archive
         * browser is up or an animation is running - it would paint over
         * the browser and fight the animation for keys. */
        QTimer *poll = new QTimer(this);
        connect(poll, &QTimer::timeout, this, [this]() {
            if (canvas->isWaiting() || canvas->isAnimating()) return;
            timer();
            canvas->refresh();
        });
        poll->start(1000);

        setWindowTitle(tr("IMAGE - radar maps"));
        statusBar()->showMessage(tr("Ready"));
    }

private:
    QScrollArea  *mapScroll = nullptr;
    SurfaceStrip *legend    = nullptr;
    SurfaceStrip *readout   = nullptr;
    CrossSectionWindow *section = nullptr;

private slots:
    /* The second click on the map: cut, and put the section on screen.  One
     * window is kept and reused, so a second cut lands where the first one
     * was rather than stacking another window on top of it. */
    void showCrossSection()
    {
        if (section == nullptr) {
            section = new CrossSectionWindow(this);
            section->setWindowFlag(Qt::Window);
            /* closing it takes the cut off the map, so repaint */
            connect(section, &CrossSectionWindow::closed, canvas,
                    &MapCanvas::refresh);
        }
        section->recut();
        section->show();
        section->raise();
    }

    void send(int key)
    {
        /* while the archive browser is up it owns the keys, and calling
         * key_pressed() again from here would re-enter it */
        if (canvas->isWaiting()) {
            canvas->injectKey(key);
            return;
        }
        if (canvas->isAnimating()) {    /* space stops the animation */
            canvas->setPolledKey(key);
            return;
        }
        if (key_pressed(key)) { close(); return; }
        canvas->refresh();
        if (section) section->dataChanged();
    }

protected:
    void closeEvent(QCloseEvent *event) override
    {
        /* closing while the archive browser waits would strand it in its
         * nested loop, and closing mid animation would run it to the end;
         * unwind them first and let the user close again */
        if (canvas->isWaiting()) {
            canvas->injectKey(KEY_ESC);
            event->ignore();
            return;
        }
        if (canvas->isAnimating()) {
            canvas->setPolledKey(' ');
            event->ignore();
            return;
        }
        QMainWindow::closeEvent(event);
    }

private:
    QPushButton *makeButton(const QString &text, int key, const QString &tip)
    {
        QPushButton *button = new QPushButton(text);
        button->setToolTip(tip);
        button->setFocusPolicy(Qt::NoFocus);   /* keep keys on the canvas */
        connect(button, &QPushButton::clicked, this, [this, key]() { send(key); });
        return button;
    }

    QGroupBox *group(const QString &title, const QList<QPushButton *> &buttons,
                     int columns)
    {
        QGroupBox *box = new QGroupBox(title);
        QGridLayout *grid = new QGridLayout(box);
        grid->setSpacing(3);
        for (int i = 0; i < buttons.size(); i++)
            grid->addWidget(buttons[i], i / columns, i % columns);
        return box;
    }

    /* One button per product, grouped by family and generated from the table
     * the core builds - there are more than forty of them, and spelling them
     * out here would only be a second copy of maps[] to keep in step.  A
     * button carries KEY_PRODUCT+index, which key_pressed() understands. */
    void buildProductGroups(QVBoxLayout *column)
    {
        struct Group {
            int family;
            const char *title;
            int columns;
        };
        static const Group groups[] = {
            { QT_FAM_DBZ,    QT_TR_NOOP("Reflectivity"),   6 },
            { QT_FAM_ZDR,    QT_TR_NOOP("ZDR"),            5 },
            { QT_FAM_VEL,    QT_TR_NOOP("Velocity"),       5 },
            { QT_FAM_SUM,    QT_TR_NOOP("Rainfall"),       5 },
        };

        for (const Group &g : groups) {
            QList<QPushButton *> buttons;
            for (int i = 0; i < product_count(); i++) {
                if (product_family_of(i) != g.family) continue;
                const int level = product_level_of(i);
                QString text = g.family == QT_FAM_DBZ && level == 0
                             ? tr("Max") : QString::number(level);
                QString tip = g.family == QT_FAM_SUM
                            ? tr("Rainfall over the last %1 h").arg(level)
                            : tr("%1, level %2").arg(tr(g.title)).arg(level);
                buttons << makeButton(text, product_key_of(i), tip);
            }
            if (!buttons.isEmpty())
                column->addWidget(group(tr(g.title), buttons, g.columns));
        }

        /* the one of a kind products keep their letters */
        QList<QPushButton *> other;
        other << makeButton(tr("Rain"), 'p', tr("Rain rate"))
              << makeButton(tr("Top"), 'h', tr("Echo top height"))
              << makeButton(tr("Phenom"), 's', tr("Phenomena"));
        column->addWidget(group(tr("Other"), other, 3));
    }

    QWidget *buildPanel()
    {
        QWidget *panel = new QWidget;
        QVBoxLayout *column = new QVBoxLayout(panel);

        buildProductGroups(column);

        QList<QPushButton *> time;
        time << makeButton(tr("<"), '-', tr("Previous time step"))
             << makeButton(tr(">"), '+', tr("Next time step"))
             << makeButton(tr("Play"), ' ', tr("Animate to the newest file"))
             << makeButton(tr("First"), '/', tr("Jump to the oldest file"))
             << makeButton(tr("Latest"), 'z', tr("Rescan and show the newest"))
             << makeButton(tr("Archive"), 'a', tr("Open the archive browser"));
        column->addWidget(group(tr("Time"), time, 3));

        QList<QPushButton *> view;
        view << makeButton(tr("+"), '.', tr("Zoom in"))
             << makeButton(tr("-"), ',', tr("Zoom out"))
             << makeButton(tr("Left"), KEY_ARROW_LEFT, tr("Pan left"))
             << makeButton(tr("Right"), KEY_ARROW_RIGHT, tr("Pan right"))
             << makeButton(tr("Up"), KEY_ARROW_UP, tr("Pan up"))
             << makeButton(tr("Down"), KEY_ARROW_DOWN, tr("Pan down"));
        column->addWidget(group(tr("View"), view, 2));

        QList<QPushButton *> overlays;
        for (int i = 1; i <= 8; i++)
            overlays << makeButton(tr("G%1").arg(i), KEY_F1 + i - 1,
                                   tr("Geography overlay %1").arg(i));
        column->addWidget(group(tr("Overlay"), overlays, 4));

        /* the archive browser is keyboard driven; these drive it by mouse.
         * Its up/down/left/right come from the View buttons above. */
        QList<QPushButton *> arch;
        arch << makeButton(tr("Enter"), KEY_ENTER, tr("Archive: open the day / confirm"))
             << makeButton(tr("Esc"), KEY_ESC, tr("Archive: close"))
             << makeButton(tr("Home"), KEY_HOME, tr("Archive: first day"))
             << makeButton(tr("End"), KEY_END, tr("Archive: last day"))
             << makeButton(tr("Mark A"), KEY_F2, tr("Archive: mark animation start"))
             << makeButton(tr("Mark B"), KEY_F3, tr("Archive: mark animation end"));
        archiveKeys = group(tr("Archive keys"), arch, 2);
        column->addWidget(archiveKeys);

        QPushButton *save = makeButton(tr("Save PNG"), 'w', tr("Write output.png"));
        column->addWidget(save);

        QPushButton *quit = new QPushButton(tr("Quit"));
        quit->setFocusPolicy(Qt::NoFocus);
        connect(quit, &QPushButton::clicked, this, &QWidget::close);
        column->addWidget(quit);

        column->addStretch(1);
        panel->setFixedWidth(panel->sizeHint().width());
        return panel;
    }

    MapCanvas *canvas;
    QGroupBox *archiveKeys = nullptr;
};

#include "qtmain.moc"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    /* QApplication switches the process over to the user's locale.  The
     * config files are parsed with scanf("%f"), which then stops at the
     * decimal point wherever the locale uses a comma - MPIX becomes 0 and
     * the map ends up blank.  Numbers stay in the C locale. */
    setlocale(LC_NUMERIC, "C");

    /* The surface is allocated once, inside image_init(), and caps how large
     * the map can ever be - so make it the size of the display rather than the
     * 1920x1080 that used to be compiled in. */
    if (QScreen *screen = app.primaryScreen()) {
        const QSize px = screen->geometry().size() * screen->devicePixelRatio();
        qt_set_screen_size(px.width(), px.height());
    }

    /* read the config, load the newest files and render the first frame */
    if (image_init(argc, argv) != 0) return 1;

    /* IMAGEQT_DEBUG=1 reports how GRX laid out the surfaces, which is what
     * one needs to explain a picture that comes out wrong on another
     * machine or another build of the library */
    if (qgetenv("IMAGEQT_DEBUG").toInt() != 0) qt_dump_surfaces();

    MainWindow window;
    window.resize(1280, 800);
    window.show();

    /* IMAGEQT_SHOT=<file> saves a picture of the window and exits, which is
     * how one checks the layout on a machine one cannot look at */
    QByteArray shot = qgetenv("IMAGEQT_SHOT");
    if (!shot.isEmpty()) {
        QTimer::singleShot(600, [&window, shot]() {
            window.grab().save(QString::fromLocal8Bit(shot));
            qApp->quit();
        });
    }

    int status = app.exec();
    close_graph();
    return status;
}
