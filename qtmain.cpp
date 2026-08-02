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

#include <clocale>

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
        setFixedSize(r.size());
        update();
    }

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
        if (animating) return 0;

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
            setFixedSize(mapRegion.size());
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
            cross_section_click();
            emit crossSectionChanged(cross_section_state());
            refresh();
        } else if (event->button() == Qt::RightButton) {
            /* the core calls this one mouse_click_left, but the X11 loop
             * only ever fed it right button presses - keep that behaviour */
            mouse_click_left(x, y);
            refresh();
        }
        setFocus();
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

        QWidget *central = new QWidget;
        QHBoxLayout *layout = new QHBoxLayout(central);
        layout->addWidget(buildPanel());
        layout->addWidget(legend,  0, Qt::AlignTop);
        layout->addWidget(mapScroll, 1);
        layout->addWidget(readout, 0, Qt::AlignTop);
        setCentralWidget(central);

        /* every pane draws from the one surface, so they repaint together */
        connect(canvas, &MapCanvas::surfaceChanged, legend,
                QOverload<>::of(&QWidget::update));
        connect(canvas, &MapCanvas::surfaceChanged, readout,
                QOverload<>::of(&QWidget::update));

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
                             "cross section; click again to restore the map"),
                        waiting ? 0 : 4000);
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

private slots:
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
