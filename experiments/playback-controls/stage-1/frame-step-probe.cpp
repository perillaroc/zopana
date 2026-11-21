#include "captureservice.h"
#include "mainwindow.h"
#include "playeractions.h"
#include "sessioncontroller.h"
#include "videosurface.h"
#include "videowidget.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QPushButton>
#include <QScopeGuard>
#include <QScreen>
#include <QSignalSpy>
#include <QTabWidget>
#include <QTableWidget>
#include <QTest>
#include <QToolButton>
#include <mpv/stream_cb.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <clocale>
#include <cmath>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <thread>

namespace {
using Id = PlayerActions::Id;

void check(bool result, const char *description)
{
    if (!result) throw std::runtime_error(description);
    std::printf("PASS %s\n", description);
}

bool until(auto predicate, int timeout = 6000)
{
    QElapsedTimer clock;
    clock.start();
    while (!predicate() && clock.elapsed() < timeout) QTest::qWait(5);
    return predicate();
}

bool near(double actual, double expected)
{
    return std::abs(actual - expected) < .00001;
}

qint64 now()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

double milliseconds(qint64 ticks) { return double(ticks) / 1'000'000; }

Player &initialize(Player &player)
{
    if (!player.initialize()) throw std::runtime_error("initialize libmpv");
    return player;
}

class RestartClock
{
public:
    explicit RestartClock(Player &player)
        : client_(mpv_create_client(player.handle(), "frame_timing"), &mpv_destroy)
    {
        if (!client_) throw std::runtime_error("create libmpv timing client");
        listener_ = std::jthread([this](std::stop_token stop) {
            while (!stop.stop_requested()) {
                const auto *event = mpv_wait_event(client_.get(), -1);
                if (event->event_id == MPV_EVENT_PLAYBACK_RESTART) lastRestart_.store(now());
            }
        });
    }

    ~RestartClock()
    {
        listener_.request_stop();
        mpv_wakeup(client_.get());
    }

    qint64 lastRestart() const { return lastRestart_.load(); }

private:
    std::unique_ptr<mpv_handle, decltype(&mpv_destroy)> client_;
    std::atomic<qint64> lastRestart_ = 0;
    std::jthread listener_;
};

qsizetype operationCount(const QSignalSpy &requests, const QString &operation)
{
    return std::ranges::count_if(requests, [&](const auto &request) { return request[3].toString() == operation; });
}

bool obsolete(const QSignalSpy &replies, quint64 id)
{
    return std::ranges::any_of(replies, [id](const auto &reply) {
        return reply[0].toULongLong() == id && !reply[1].toBool();
    });
}

QVector<double> readTimestamps(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) throw std::runtime_error("open ffprobe timestamps");
    QVector<double> result;
    for (const auto &frame : QJsonDocument::fromJson(file.readAll()).object().value("frames").toArray()) {
        bool valid = false;
        const double time = frame.toObject().value("best_effort_timestamp_time").toString().toDouble(&valid);
        if (valid) result.append(time);
    }
    check(!result.isEmpty() && std::ranges::is_sorted(result), "FFprobe frame timestamps are in presentation order");
    return result;
}

double previousFrame(const QVector<double> &frames, double from)
{
    const auto it = std::ranges::find_if(frames, [from](double pts) { return near(pts, from); });
    if (it == frames.cbegin() || it == frames.cend()) throw std::runtime_error("FFprobe missing the requested frame or its predecessor");
    return *(it - 1);
}

QByteArray displayedRows(VideoWidget &video)
{
    // Reading the existing FBO after frameSwapped avoids grabFramebuffer(),
    // which invokes paintGL and could render the target before it is shown.
    const QSize size = video.size() * video.devicePixelRatioF();
    constexpr int rowCount = 7;
    QByteArray rows(size.width() * rowCount * 4, Qt::Uninitialized);
    video.makeCurrent();
    auto *gl = video.context()->functions();
    for (int row = 0; row < rowCount; ++row) {
        const int y = size.height() * (row + 1) / (rowCount + 1);
        gl->glReadPixels(0, y, size.width(), 1, GL_RGBA, GL_UNSIGNED_BYTE, rows.data() + row * size.width() * 4);
    }
    video.doneCurrent();
    return rows;
}

int maxChannelDifference(QByteArrayView a, QByteArrayView b)
{
    if (a.size() != b.size()) return 255;
    int difference = 0;
    for (qsizetype i = 0; i < a.size(); ++i)
        difference = std::max(difference, std::abs(int(uchar(a[i])) - int(uchar(b[i]))));
    return difference;
}

QRect greenMarker(const QImage &image)
{
    QRect marker;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QRgb pixel = image.pixel(x, y);
            if (qGreen(pixel) > 160 && qGreen(pixel) > qRed(pixel) + 80 && qGreen(pixel) > qBlue(pixel) + 80)
                marker |= QRect(x, y, 1, 1);
        }
    }
    return marker;
}

struct Probe
{
    const QDir fixtures, evidence;
    QString streamFile;
    SettingsStore settings;
    HistoryStore history;
    Player player;
    MainWindow main;
    SessionController session;
    PlayerActions *actions = main.findChild<PlayerActions *>();
    VideoWidget *video = main.findChild<VideoWidget *>("videoWidget");
    QWidget *control = main.findChild<QWidget *>("controlWindow");
    QToolButton *backButton = control->findChild<QToolButton *>("controlFrameBackward");
    QAction *back = actions->get(Id::FrameBackward);
    QSignalSpy requests{&player, &Player::requestSubmitted};
    QSignalSpy replies{&player, &Player::requestFinished};

    Probe(const QString &fixtureDir, const QString &output)
        : fixtures(fixtureDir), evidence(output), streamFile(evidence.filePath("cfr.mkv")),
          settings(output + "/config"), history(output + "/data"), main(initialize(player)),
          session(player, main, settings, history)
    {
        auto preferences = session.preferences();
        preferences.recordHistory = false;
        preferences.captureDirectory = evidence.filePath("captures");
        check(session.applyPreferences(preferences), "configure an isolated profile and screenshot destination");
        main.resize(1280, 820);
        main.show();
        activate(main);
        check(until([&] { return video->rendererReady(); }), "OpenGL widget is ready");
        std::printf("Qt %s, DPR %.2f\n", qVersion(), video->devicePixelRatioF());
    }

    void activate(QWidget &window)
    {
        window.raise();
        window.activateWindow();
        if (!QTest::qWaitForWindowActive(&window)) throw std::runtime_error("cannot activate probe window");
        if (&window == &main) {
            video->setFocus();
            if (!until([&] { return video->hasFocus() && actions->viewingInputAllowed(); }))
                throw std::runtime_error("focus the main viewing area");
        }
    }

    void openVideo(const QString &path)
    {
        const auto generation = player.generation();
        main.ingestPaths({path}, true);
        check(until([&] { return player.generation() > generation && player.canFrameBackStep() && back->isEnabled()
            && video->canCaptureTransform(player.videoTransformVersion()); }), "seekable video enables the shared action");
        player.setPaused(true);
        check(until([&] { return player.paused(); }), "video paused");
    }

    void at(double time, const char *message)
    {
        const bool matches = until([&] { return player.paused() && !player.seekInProgress() && near(player.position(), time); });
        std::printf("POS %.6f expected %.6f paused=%d\n", player.position(), time, player.paused());
        check(matches, message);
    }

    void prime(double time)
    {
        player.seekFrom(time, time);
        at(time, "independent exact seek");
        QTest::qWait(60);
    }

    void key(QEvent::Type type, Qt::Key key, bool repeat = false)
    {
        QKeyEvent event(type, key, Qt::NoModifier, {}, repeat);
        QApplication::sendEvent(video, &event);
    }

    void clickMenuAction(const char *name, QAction *action)
    {
        auto *menu = main.findChild<QMenu *>(name);
        menu->popup(video->mapToGlobal(QPoint(20, 20)));
        if (!until([&] { return menu->isVisible(); })) throw std::runtime_error("open the player menu");
        QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier, menu->actionGeometry(action).center());
    }

    void menuBack() { clickMenuAction("playMenu", back); }

    void bindShortcut()
    {
        const auto *menu = main.findChild<QMenu *>("playMenu");
        check(backButton->defaultAction() == back && menu->actions().contains(back), "playback menu and tool button use the same action");
        check(!back->isEnabled() && !backButton->isEnabled() && back->shortcuts().isEmpty()
            && !back->autoRepeat() && actions->get(Id::FrameForward)->shortcut() == QKeySequence(Qt::Key_D),
            "no media disables reverse step and D remains the forward shortcut");

        bool edited = false;
        QTimer::singleShot(0, &main, [&] {
            auto *dialog = main.findChild<QDialog *>("settingsDialog");
            if (!dialog) return;
            auto *table = dialog->findChild<QTableWidget *>("shortcutTable");
            dialog->findChild<QTabWidget *>("settingsTabs")->setCurrentIndex(1);
            dialog->findChild<QLineEdit *>("shortcutFilter")->setText(back->text());
            if (table->rowCount() == 1 && table->item(0, 1)->data(Qt::UserRole).toString() == back->objectName()) {
                table->setCurrentCell(0, 1);
                dialog->findChild<QKeySequenceEdit *>("shortcutFirst")->setKeySequence(QKeySequence(Qt::Key_Z));
                dialog->findChild<QPushButton *>("setBindingButton")->click();
                dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Apply)->click();
                edited = true;
            }
            dialog->reject();
        });
        actions->get(Id::Settings)->trigger();
        check(edited && back->shortcut() == QKeySequence(Qt::Key_Z)
            && settings.load().shortcuts.value(back->objectName()).toStringList() == QStringList{"Z"},
            "settings exposes and persists a custom viewing shortcut");
        activate(main);
    }

    void compareFrames(const QString &name)
    {
        openVideo(evidence.filePath(name + ".mkv"));
        const auto frames = readTimestamps(evidence.filePath(name + "-frames.json"));
        for (double from : {1.2, 4.4, 7.44}) {
            const double target = previousFrame(frames, from);
            prime(from);
            const auto before = video->grabFramebuffer();
            requests.clear();
            menuBack();
            at(target, "reverse step lands on the FFprobe predecessor");
            const auto stepped = video->grabFramebuffer();
            check(operationCount(requests, "frame-back-step ") == 1 && operationCount(requests, "get pause") == 0,
                  "paused menu action submits exactly one direct reverse step");
            prime(target);
            const auto reference = video->grabFramebuffer();
            check(!stepped.isNull() && stepped != before && stepped == reference,
                  "the stepped image equals the independent exact seek image");
            const auto prefix = QString("%1-%2").arg(name).arg(from, 0, 'f', 2);
            check(stepped.save(evidence.filePath(prefix + "-step.png")) &&
                  reference.save(evidence.filePath(prefix + "-seek.png")), "save stepped and reference images");
        }
    }

    void transport()
    {
        openVideo(evidence.filePath("cfr.mkv"));
        prime(.04);
        back->trigger();
        at(0, "the second frame steps to the first");
        const auto first = video->grabFramebuffer();
        for (int attempt = 0; attempt < 2; ++attempt) {
            back->trigger();
            at(0, "the first frame is a stable boundary");
            check(video->grabFramebuffer() == first && back->isEnabled(), "first-frame attempts preserve the picture and release the action");
        }
        activate(main);
        QTest::keyClick(video, Qt::Key_D);
        at(.04, "D still advances one frame from the beginning");

        prime(3);
        player.seek(4.4);
        at(4.4, "manual seek sets up an undo record");
        check(player.canUndoSeek(), "manual seek can be undone before stepping");
        actions->get(Id::ControlWindowToggle)->trigger();
        activate(*control);
        requests.clear();
        QTest::mouseClick(backButton, Qt::LeftButton);
        check(player.frameBackStepInProgress() && !backButton->isEnabled(), "tool button becomes busy during its reverse seek");
        player.frameBackStep();
        at(4.36, "the tool button steps back while paused");
        check(operationCount(requests, "frame-back-step ") == 1, "a second request is ignored while the first is pending");
        check(player.canUndoSeek(), "reverse step leaves the manual seek undo available");
        control->close();
        activate(main);
        auto *forward = actions->get(Id::FrameForward);
        QSignalSpy forwardKeys(forward, &QAction::triggered);
        QTest::keyClick(video, Qt::Key_D);
        check(forwardKeys.size() == 1, "D triggers the shared forward action once after closing the tool window");
        at(4.4, "D goes forward after reversing");
        check(player.canUndoSeek(), "forward step also preserves the undo record");
        actions->get(Id::UndoSeek)->trigger();
        at(3, "undo returns to the original manual seek position after both frame steps");

        player.setPaused(false);
        check(until([&] { return !player.paused() && player.position() > 3.08; }), "video is playing before reverse step");
        QSignalSpy pauseStates(&player, &Player::pausedChanged);
        requests.clear();
        QTest::keyClick(video, Qt::Key_Z);
        check(until([&] { return !player.frameBackStepInProgress() && player.paused(); }), "the custom shortcut pauses playback and leaves it paused after stepping");
        QStringList operations;
        for (const auto &request : requests) operations.append(request[3].toString());
        check(!pauseStates.isEmpty() && operations == QStringList{"set ", "get pause", "frame-back-step ", "get time-pos"},
              "playing reverse step waits for a confirmed pause before issuing the seek");
    }

    void retryAndCancellation()
    {
        openVideo(evidence.filePath("cfr.mkv"));
        prime(4.4);
        check(mpv_request_event(player.handle(), MPV_EVENT_PLAYBACK_RESTART, 0) == 0, "simulate a missing back-step restart event");
        back->trigger();
        check(until([&] { return !player.frameBackStepInProgress() && back->isEnabled(); }, 6500), "a missing restart times out and re-enables the action");
        check(near(player.position(), 4.4), "an unconfirmed step does not publish the requested position");
        check(mpv_request_event(player.handle(), MPV_EVENT_PLAYBACK_RESTART, 1) == 0, "restore playback restart events");
        back->trigger();
        at(4.32, "retry steps backward from the engine's current frame");

        player.setPaused(false);
        check(until([&] { return !player.paused() && player.position() > 4.4; }), "play again before switching with a pause request pending");
        requests.clear();
        back->trigger();
        check(operationCount(requests, "set ") == 1, "reverse step has a pending pause command");
        const auto pauseId = requests.front()[0].toULongLong();
        main.ingestPaths({evidence.filePath("vfr.mkv")}, true);
        check(until([&] { return obsolete(replies, pauseId) && back->isEnabled() && player.position() > .16; })
            && player.position() < 1 && !player.paused() && player.path() == evidence.filePath("vfr.mkv")
            && operationCount(requests, "frame-back-step ") == 0, "switching discards the old pause reply without stepping or pausing the new file");

        player.setPaused(true);
        check(until([&] { return player.paused(); }), "pause before testing a late position reply");
        prime(4.4);
        quint64 positionId = 0;
        const auto switchOnRead = QObject::connect(&player, &Player::requestSubmitted, &main,
            [&](quint64 id, quint64, quint64, const QString &operation) {
                if (operation != "get time-pos" || positionId) return;
                positionId = id;
                main.ingestPaths({evidence.filePath("cfr.mkv")}, true);
            });
        const auto disconnect = qScopeGuard([&] { QObject::disconnect(switchOnRead); });
        back->trigger();
        check(until([&] { return positionId && obsolete(replies, positionId) && back->isEnabled() && player.position() > .16; })
            && player.position() < 1 && !player.paused() && player.path() == evidence.filePath("cfr.mkv"),
            "switching discards a late position reply after the engine completed the reverse seek");

        player.setPaused(true);
        check(until([&] { return player.paused(); }), "pause before testing stop");
        prime(4.4);
        requests.clear();
        back->trigger();
        check(operationCount(requests, "frame-back-step ") == 1, "reverse seek is pending before stop");
        const auto stopId = requests.front()[0].toULongLong();
        player.stop();
        check(until([&] { return obsolete(replies, stopId) && !back->isEnabled(); })
            && !player.seekInProgress() && player.state() == Player::State::Idle && !player.canUndoSeek(),
            "stop clears the pending step and discards the old seek reply");
    }

    void rendererRecovery()
    {
        openVideo(evidence.filePath("cfr.mkv"));
        prime(3);
        player.seek(4.4);
        at(4.4, "manual seek before rebuilding the renderer");
        const auto firstEpoch = player.renderEpoch();
        auto *parent = video->parentWidget();
        const auto geometry = video->geometry();
        QWidget otherWindow;
        otherWindow.resize(geometry.size());
        const auto returnVideo = [&] {
            if (video->parentWidget() == parent) return;
            video->setParent(parent);
            video->setGeometry(geometry);
            video->show();
        };
        const auto guard = qScopeGuard(returnVideo);
        requests.clear();
        back->trigger();
        check(operationCount(requests, "frame-back-step ") == 1, "reverse seek is pending before the OpenGL context is destroyed");
        const auto stepId = requests.front()[0].toULongLong();
        video->setParent(&otherWindow);
        video->setGeometry(otherWindow.rect());
        otherWindow.show();
        video->show();
        check(until([&] { return player.renderEpoch() > firstEpoch && !player.recovering() && back->isEnabled(); }),
              "reparenting rebuilds the real OpenGL context");
        at(4.4, "renderer recovery keeps the pre-step frame and pause state");
        check(obsolete(replies, stepId) && player.canUndoSeek(), "the old step reply is discarded while manual undo survives");
        returnVideo();
        otherWindow.close();
        check(until([&] { return player.renderEpoch() > firstEpoch + 1 && !player.recovering() && back->isEnabled(); }),
              "restored video returns to the main window");
        activate(main);
        back->trigger();
        at(4.36, "reverse stepping works on the rebuilt renderer");
    }

    void unavailableMedia()
    {
        for (const auto &path : {fixtures.filePath("audio.m4a"), evidence.filePath("audio-cover.m4a")}) {
            const auto generation = player.generation();
            main.ingestPaths({path}, true);
            check(until([&] { return player.generation() > generation && player.state() == Player::State::Loaded
                && !player.tracks().isEmpty() && !back->isEnabled(); }), "audio has no reverse-step action");
            if (path.endsWith("audio-cover.m4a"))
                check(until([&] { return video->canCapture() && !backButton->isEnabled(); }), "embedded album art does not enable reversing");
        }
        check(mpv_stream_cb_add_ro(player.handle(), "validationstream", &streamFile,
            [](void *path, char *, mpv_stream_cb_info *info) {
                auto file = std::make_unique<QFile>(*static_cast<const QString *>(path));
                if (!file->open(QIODevice::ReadOnly)) return int(MPV_ERROR_LOADING_FAILED);
                info->cookie = file.release();
                info->read_fn = [](void *data, char *buffer, uint64_t size) -> int64_t {
                    return static_cast<QFile *>(data)->read(buffer, qint64(size));
                };
                info->close_fn = [](void *data) { delete static_cast<QFile *>(data); };
                return 0;
            }) == 0, "register a video stream with no seek callback");
        player.openFile("validationstream://cfr.mkv");
        check(until([&] { return player.state() == Player::State::Loaded && player.videoTransformReady() && video->canCapture(); })
            && !player.seekable() && !back->isEnabled() && !backButton->isEnabled(),
            "a rendered but non-seekable video disables reverse step");
        requests.clear();
        player.frameBackStep();
        check(requests.isEmpty(), "the player also rejects direct reverse stepping on a non-seekable video");
    }

    QImage capture(Id id, QString *path = nullptr)
    {
        QSignalSpy saved(main.findChild<CaptureService *>(), &CaptureService::saved);
        actions->get(id)->trigger();
        check(until([&] { return saved.size() == 1; }), "production capture saves a PNG");
        const auto savedPath = saved.front()[0].toString();
        if (path) *path = savedPath;
        return QImage(savedPath);
    }

    void framing()
    {
        openVideo(evidence.filePath("marker.mkv"));
        actions->get(Id::RotateNext)->trigger();
        check(until([&] { return player.videoRotation() == 90 && back->isEnabled(); }), "90-degree rotation is ready");
        video->setZoomScale(8);
        check(video->zoomScale() == 8, "8× zoom applied");
        prime(1.04);
        back->trigger();
        at(1, "reverse step reaches the green-marker frame");
        const auto source = capture(Id::CaptureSource);
        const auto crop = capture(Id::CaptureViewport);
        check(source.size() == QSize(360, 640) && crop.size() == video->size() * video->devicePixelRatioF(),
              "the rotated source keeps full dimensions and the crop uses viewport pixels");
        const auto sourceMarker = greenMarker(source);
        const auto cropMarker = greenMarker(crop);
        const QPointF target(194, 320);
        const QPointF expectedCrop(crop.width() / 2.0 + (target.x() - source.width() / 2.0) * 8 * crop.height() / source.height(),
                                   crop.height() / 2.0 + (target.y() - source.height() / 2.0) * 8 * crop.height() / source.height());
        std::printf("MARKER source=%d,%d viewport=%d,%d expected viewport=%.2f,%.2f\n",
            sourceMarker.center().x(), sourceMarker.center().y(), cropMarker.center().x(), cropMarker.center().y(), expectedCrop.x(), expectedCrop.y());
        check(sourceMarker.isValid() && (QPointF(sourceMarker.center()) - target).manhattanLength() <= 3
            && cropMarker.isValid() && (QPointF(cropMarker.center()) - expectedCrop).manhattanLength() < 20,
            "both capture modes place the green marker at its expected rotated coordinates");
        prime(1);
        check(source == capture(Id::CaptureSource), "the full-size image exactly matches an independent seek");
        const auto actual = crop.convertToFormat(QImage::Format_RGBA8888);
        const auto reference = capture(Id::CaptureViewport).convertToFormat(QImage::Format_RGBA8888);
        const auto bytes = [](const QImage &image) {
            return QByteArrayView(reinterpret_cast<const char *>(image.constBits()), image.sizeInBytes());
        };
        // GPU interpolation can round a cropped edge to the next 8-bit value.
        const int difference = maxChannelDifference(bytes(actual), bytes(reference));
        std::printf("CROP maximum channel difference from reference: %d\n", difference);
        check(actual.size() == reference.size() && difference <= 1,
              "the cropped image agrees with the independent seek within one 8-bit level");
        actions->get(Id::RotateReset)->trigger();
        check(until([&] { return player.videoRotation() == 0 && back->isEnabled(); }), "restore orientation for timing");
        video->resetView();
    }

    void referenceComparison()
    {
        auto *captures = main.findChild<CaptureService *>();
        auto *pin = actions->get(Id::PinReferenceFrame);
        auto *toggle = actions->get(Id::ToggleReferenceFrame);
        auto *clear = actions->get(Id::ClearReferenceFrame);
        auto *picture = main.findChild<QMenu *>("pictureMenu");
        auto *layer = main.findChild<QLabel *>("referenceFrameImage");
        auto *notice = main.findChild<QLabel *>("referenceFrameNotice");
        auto *pinButton = control->findChild<QToolButton *>("controlPinReferenceFrame");
        auto *toggleButton = control->findChild<QToolButton *>("controlToggleReferenceFrame");
        auto *clearButton = control->findChild<QToolButton *>("controlClearReferenceFrame");
        const auto visible = [&] { return layer->isVisible() && notice->isVisible(); };
        const auto noReference = [&] {
            return !captures->hasReference() && !toggle->isEnabled() && !clear->isEnabled()
                && !visible() && layer->pixmap().isNull();
        };
        check(!pin->isEnabled() && noReference(), "reference operations are disabled without video");
        check(pinButton->defaultAction() == pin && toggleButton->defaultAction() == toggle
            && clearButton->defaultAction() == clear && picture->actions().contains(pin)
            && picture->actions().contains(toggle) && picture->actions().contains(clear),
            "the Picture menu and framing tab share all three reference actions");

        openVideo(evidence.filePath("marker.mkv"));
        actions->get(Id::RotateNext)->trigger();
        check(until([&] { return player.videoRotation() == 90 && pin->isEnabled(); }), "the rotated video can be pinned");
        video->restoreView(8, {.505, .46});
        prime(.96);
        player.seek(1);
        at(1, "seek to the green marker and establish an undo point");
        const auto rgb = [](const QImage &image) { return image.convertToFormat(QImage::Format_RGB32); };
        const auto viewport = [&] { return rgb(captures->viewportImage()); };
        const auto green = viewport();
        check(greenMarker(green).isValid() && green.size() == video->framing().viewportPixels
            && video->zoomScale() == 8 && player.canUndoSeek(), "the 90-degree 8× crop contains the green marker");
        actions->get(Id::ControlWindowToggle)->setChecked(true);
        activate(*control);
        control->findChild<QTabWidget *>("controlTabs")->setCurrentIndex(1);
        QTest::mouseClick(pinButton, Qt::LeftButton);
        check(toggleButton->isEnabled() && clearButton->isEnabled() && !visible(),
            "pinning from the framing tab leaves the current view visible");

        activate(main);
        const auto playback = player.playbackIdentity();
        const auto beforeToggle = requests.size();
        clickMenuAction("pictureMenu", toggle);
        check(visible() && notice->text().contains("00:00:01.000")
            && toggleButton->text() == QStringLiteral("返回当前画面")
            && rgb(layer->grab().toImage()) == green, "the Picture menu shows the exact reference pixels with a separate time label");
        check(playback == player.playbackIdentity() && player.paused() && near(player.position(), 1)
            && player.canUndoSeek() && requests.size() == beforeToggle, "toggling the reference queues no playback operation");

        activate(*control);
        QTest::mouseClick(control->findChild<QToolButton *>("controlReferenceFrameForward"), Qt::LeftButton);
        check(!visible() && toggle->isEnabled(), "the framing-tab forward step returns to live and retains the reference");
        at(1.04, "forward step reaches the blue-marker frame");
        const auto blue = viewport();
        check(!blue.isNull() && blue != green && !greenMarker(blue).isValid(),
            "the next frame differs from the pinned green-marker frame");

        const auto checkOutputs = [&](const QImage &expected, const QString &name, const QString &time) {
            QTest::mouseClick(control->findChild<QToolButton *>("controlCopyViewport"), Qt::LeftButton);
            const auto clipboard = rgb(QApplication::clipboard()->image());
            QString file;
            const auto screenshot = rgb(capture(Id::CaptureViewport, &file));
            check(clipboard == expected && screenshot == expected
                && QFileInfo(file).fileName().contains('-' + time + "s-viewport-"),
                "clipboard and viewport PNG match the shown frame, with its time in the filename");
            check(clipboard.save(evidence.filePath(name + "-clipboard.png"))
                && QFile::copy(file, evidence.filePath(name + "-viewport.png")), "retain image output evidence");
        };
        checkOutputs(blue, "live", "1.040");
        const auto liveSource = capture(Id::CaptureSource);
        QTest::mouseClick(toggleButton, Qt::LeftButton);
        check(visible() && viewport() == green && near(player.position(), 1.04) && player.paused(),
            "the pinned green view can be compared against the blue frame without moving playback");
        main.findChild<VideoSurface *>()->dismissOsd();
        check(main.grab().save(evidence.filePath("reference-window.png"))
            && control->grab().save(evidence.filePath("reference-controls.png")), "retain the reference display and controls");
        checkOutputs(green, "reference", "1.000");
        check(capture(Id::CaptureSource) == liveSource && liveSource.size() == QSize(360, 640),
            "the full-frame screenshot still comes from the current live frame");

        QTest::mouseClick(control->findChild<QToolButton *>("controlReferenceFrameBackward"), Qt::LeftButton);
        check(!visible() && toggle->isEnabled(), "the framing-tab backward step also returns to live");
        at(1, "backward step returns to the original green frame");
        check(greenMarker(viewport()) == greenMarker(green) && player.canUndoSeek(), "reverse step restores the marker without replacing the undo point");
        toggle->trigger();
        actions->get(Id::UndoSeek)->trigger();
        check(!visible() && toggle->isEnabled(), "undo returns to live while keeping the reference available");
        at(.96, "undo reaches the position recorded before fixing the reference");
        toggle->trigger();
        player.seek(1.04);
        check(!visible() && toggle->isEnabled(), "a manual seek returns to live and keeps the reference");
        at(1.04, "manual seek returns to the blue marker");

        toggle->trigger();
        video->setVideoTransform(std::nullopt);
        check(!captures->pinReference() && viewport() == green && visible(),
            "an unavailable live capture leaves the shown reference in place");
        video->setVideoTransform(player.videoTransformVersion());
        check(until([&] { return video->canCaptureTransform(player.videoTransformVersion()); }), "live output becomes available for replacement");
        pin->trigger();
        check(visible() && viewport() == blue && notice->text().contains("00:00:01.040"),
            "pinning over a reference replaces it with the actual live frame");

        auto *hold = control->findChild<QToolButton *>("controlFullView");
        QTest::mousePress(hold, Qt::LeftButton);
        check(!visible() && !toggle->isEnabled() && video->zoomScale() == 1, "hold to fit starts from live video");
        QTest::mouseRelease(hold, Qt::LeftButton);
        check(!visible() && toggle->isEnabled() && video->zoomScale() == 8,
            "releasing hold to fit restores the crop and keeps the reference");
        toggle->trigger();
        video->restoreView(video->zoomScale(), video->viewFocus());
        check(visible() && viewport() == blue, "an unchanged view preserves the pinned reference");

        player.setPaused(false);
        check(until([&] { return !player.paused() && player.position() > 1.16; }) && visible() && viewport() == blue,
            "live playback advances while the pinned frame remains visible");
        toggle->trigger();
        check(!visible() && !player.paused() && viewport() != blue, "returning to live shows the advancing video");
        player.setPaused(true);
        check(until([&] { return player.paused(); }), "pause for framing and lifecycle checks");

        const auto pinAndShow = [&] {
            check(until([&] { return pin->isEnabled(); }), "live frame available before pinning");
            pin->trigger();
            toggle->trigger();
            check(visible(), "show the pinned frame before the next operation");
        };
        toggle->trigger();
        actions->get(Id::ZoomOut)->trigger();
        check(until([&] { return noReference() && near(video->zoomScale(), 8 / 1.15); })
            && main.findChild<QLabel *>("playerOsd")->text().contains(QStringLiteral("重新固定")),
            "a zoom change discards the reference and shows a repin hint");
        pinAndShow();
        const auto beforePan = video->framing();
        video->panBy({35, 28});
        check(video->framing() != beforePan && noReference(), "a visible pan discards the reference");
        pinAndShow();
        const auto beforeResize = video->framing().viewportPixels;
        main.resize(main.width() - 100, main.height());
        check(until([&] { return video->framing().viewportPixels != beforeResize && noReference(); }),
            "a physical viewport resize discards the reference");
        pinAndShow();
        actions->get(Id::RotateNext)->trigger();
        check(until([&] { return player.videoRotation() == 180 && noReference(); }), "a confirmed rotation discards the reference");

        pinAndShow();
        player.openFile(fixtures.filePath("grid.mp4"));
        player.openFile(fixtures.filePath("portrait.mp4"));
        check(noReference() && !pin->isEnabled(), "rapid file replacement releases the old reference immediately");
        check(until([&] { return player.path() == fixtures.filePath("portrait.mp4") && pin->isEnabled(); })
            && noReference(), "the replacement file starts without a reference");
        pinAndShow();
        const auto epoch = player.renderEpoch();
        auto *parent = video->parentWidget();
        const auto geometry = video->geometry();
        QWidget otherWindow;
        video->setParent(&otherWindow);
        const bool clearedOnRebuild = player.renderEpoch() > epoch && noReference() && !pin->isEnabled();
        video->setParent(parent);
        video->setGeometry(geometry);
        video->lower();
        video->show();
        check(clearedOnRebuild, "destroying the OpenGL context releases the reference");
        check(until([&] { return pin->isEnabled(); }) && noReference(), "the recovered renderer starts without a reference");
        pinAndShow();
        QTest::mouseClick(clearButton, Qt::LeftButton);
        check(noReference() && pin->isEnabled(), "clearing from the framing tab returns to live and releases the image");
        pinAndShow();
        player.stop();
        check(noReference() && !pin->isEnabled(), "stopping releases the reference and disables the controls");
    }

    void time4K()
    {
        openVideo(fixtures.filePath("motion-4k.mp4"));
        activate(main);
        const auto viewport = video->size() * video->devicePixelRatioF();
        video->makeCurrent();
        const auto *renderer = video->context()->functions()->glGetString(GL_RENDERER);
        std::printf("4K viewport %dx%d pixels, %.2f Hz, GL %s\n", viewport.width(), viewport.height(),
            main.screen()->refreshRate(), reinterpret_cast<const char *>(renderer));
        video->doneCurrent();
        const auto frames = readTimestamps(evidence.filePath("4k-frames.json"));
        RestartClock clock(player);
        const auto start = now();
        QJsonArray timings;
        for (double from : {1.266667, 4.733333, 8.833333, 11.566667, 13.766667, 16.333333}) {
            const double target = previousFrame(frames, from);
            prime(target);
            const auto expected = displayedRows(*video);
            prime(from);
            check(maxChannelDifference(displayedRows(*video), expected) > 1, "sampled framebuffer rows distinguish adjacent 4K frames");
            qint64 shown = 0;
            const auto connection = QObject::connect(video, &QOpenGLWidget::frameSwapped, &main, [&] {
                const auto swapped = now();
                if (!shown && maxChannelDifference(displayedRows(*video), expected) <= 1) shown = swapped;
            });
            const auto disconnect = qScopeGuard([&] { QObject::disconnect(connection); });
            requests.clear();
            const auto pressed = now();
            key(QEvent::KeyPress, Qt::Key_Z);
            key(QEvent::KeyRelease, Qt::Key_Z);
            const bool reached = until([&] { return shown && clock.lastRestart() > pressed && !player.seekInProgress(); });
            check(reached && near(player.position(), target) && player.paused() && operationCount(requests, "frame-back-step ") == 1,
                "one shortcut shows the independent 4K reference frame and stays paused");
            const auto engine = clock.lastRestart();
            std::printf("4K %.6f -> %.6f, key %.3f, restart %.3f, swap %.3f, latency %.3f / %.3f ms\n",
                from, player.position(), milliseconds(pressed - start), milliseconds(engine - start),
                milliseconds(shown - start), milliseconds(engine - pressed), milliseconds(shown - pressed));
            timings.append(QJsonObject{{"from", from}, {"target", target}, {"confirmed", player.position()},
                {"keyMs", milliseconds(pressed - start)}, {"restartMs", milliseconds(engine - start)},
                {"swapMs", milliseconds(shown - start)}, {"keyToRestartMs", milliseconds(engine - pressed)},
                {"keyToSwapMs", milliseconds(shown - pressed)}});
        }
        QFile log(evidence.filePath("4k-timings.json"));
        check(log.open(QIODevice::WriteOnly) && log.write(QJsonDocument(timings).toJson()) > 0, "write the six 4K timing samples");

        prime(8.833333);
        const auto before = displayedRows(*video);
        requests.clear();
        key(QEvent::KeyPress, Qt::Key_Z);
        for (int repeat = 0; repeat < 24; ++repeat) {
            QTest::qWait(40);
            key(QEvent::KeyPress, Qt::Key_Z, true);
        }
        key(QEvent::KeyRelease, Qt::Key_Z);
        at(8.8, "holding and releasing the custom key produces one reverse step");
        QTest::qWait(250);
        check(near(player.position(), 8.8) && operationCount(requests, "frame-back-step ") == 1,
              "holding past engine completion does not queue another step");
        QTest::keyClick(video, Qt::Key_D);
        at(8.833333, "D advances to the original 4K frame once");
        QTest::qWait(100);
        check(maxChannelDifference(displayedRows(*video), before) <= 1 && operationCount(requests, "frame-step ") == 1,
              "the 4K image returns to the original without additional stepping");
    }
};
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setApplicationName("zopana");
    std::setlocale(LC_NUMERIC, "C");
    const auto args = app.arguments();
    const bool reference = args.size() == 4 && args[3] == "--reference";
    if (!reference && args.size() != 3) return 2;
    try {
        Probe probe(args[1], args[2]);
        if (reference) {
            probe.referenceComparison();
            return 0;
        }
        probe.bindShortcut();
        probe.compareFrames("cfr");
        probe.compareFrames("vfr");
        probe.transport();
        probe.retryAndCancellation();
        probe.rendererRecovery();
        probe.unavailableMedia();
        probe.framing();
        probe.time4K();
    } catch (const std::exception &error) {
        std::fprintf(stderr, "FAIL %s\n", error.what());
        return 1;
    }
}
