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

#include <clocale>
#include <stdio.h>

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

    QSize sizeHint() const override { return image.size(); }

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

        update();
        animating = true;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        animating = false;

        int key = polledKey;
        polledKey = 0;
        return key;
    }

    bool isWaiting() const { return waiting; }
    bool isAnimating() const { return animating; }
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

        /* GRX RAM24 stores the components in BGR order */
        if (pixels && w > 0 && h > 0) {
            image = QImage(pixels, w, h, qt_screen_stride(), QImage::Format_BGR888);
            setFixedSize(w, h);
        }

        unsigned char *map = qt_map_pixels();
        int mw = qt_map_width();
        int mh = qt_map_height();
        if (map && mw > 0 && mh > 0) {
            mapImage = QImage(map, mw, mh, qt_map_stride(), QImage::Format_BGR888);
            mapOrigin = QPoint(qt_map_origin_x(), qt_map_origin_y());
        }
    }

signals:
    void positionChanged(int x, int y);
    void quitRequested();
    void waitStateChanged(bool waiting);

protected:
    /* The archive browser and the animation both read keys themselves, and
     * neither can rely on this widget holding the focus - a button may have
     * it.  Grab keys for them here; otherwise let them take the normal
     * route to keyPressEvent(). */
    bool eventFilter(QObject *object, QEvent *event) override
    {
        if (event->type() == QEvent::FocusIn || event->type() == QEvent::WindowActivate)
            fprintf(stderr, "DBG: focus/activate event %d on %s\n", (int)event->type(),
                    object->metaObject()->className());
        if (event->type() != QEvent::KeyPress)
            return QWidget::eventFilter(object, event);
        int code = translate(static_cast<QKeyEvent *>(event));
        {   QKeyEvent *ke = static_cast<QKeyEvent *>(event);
            fprintf(stderr, "DBG: KeyPress target=%s key=0x%x text='%s' -> code=%d\n",
                    object->metaObject()->className(), ke->key(),
                    ke->text().toUtf8().constData(), code); }
        if (code < 0)
            return QWidget::eventFilter(object, event);

        if (waiting)        injectKey(code);   /* archive browser  */
        else if (animating) polledKey = code;  /* animation loop   */
        else {                                 /* normal operation */
            if (key_pressed(code)) emit quitRequested();
            update();
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
        painter.drawImage(event->rect(), image, event->rect());
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        mouse_move(event->x(), event->y());
        emit positionChanged(event->x(), event->y());
        update();
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        /* the core calls this one mouse_click_left, but the event loop only
         * ever fed it right button presses - keep that behaviour */
        if (event->button() == Qt::RightButton) {
            mouse_click_left(event->x(), event->y());
            update();
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

        QScrollArea *scroll = new QScrollArea;
        scroll->setWidget(canvas);
        scroll->setAlignment(Qt::AlignCenter);
        scroll->setBackgroundRole(QPalette::Dark);

        QWidget *central = new QWidget;
        QHBoxLayout *layout = new QHBoxLayout(central);
        layout->addWidget(buildPanel());
        layout->addWidget(scroll, 1);
        setCentralWidget(central);

        connect(canvas, &MapCanvas::quitRequested, this, &QWidget::close);
        connect(canvas, &MapCanvas::waitStateChanged, this,
                [this](bool waiting) {
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
            canvas->update();
        });
        poll->start(1000);

        setWindowTitle(tr("IMAGE - radar maps"));
        statusBar()->showMessage(tr("Ready"));
    }

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
        canvas->update();
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

    QWidget *buildPanel()
    {
        QWidget *panel = new QWidget;
        QVBoxLayout *column = new QVBoxLayout(panel);

        QList<QPushButton *> layers;
        layers << makeButton(tr("Max"), 'p', tr("Maximum reflectivity"))
               << makeButton(tr("Top"), 'h', tr("Echo top height"))
               << makeButton(tr("Storm"), 's', tr("Storm cells"))
               << makeButton(tr("Sum"), 'q', tr("Accumulated rainfall"));
        for (int i = 0; i <= 8; i++)
            layers << makeButton(QString::number(i), '0' + i,
                                 tr("Constant altitude level %1").arg(i));
        column->addWidget(group(tr("Product"), layers, 4));

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

    MainWindow window;
    window.resize(1280, 800);
    window.show();

    int status = app.exec();
    close_graph();
    return status;
}
