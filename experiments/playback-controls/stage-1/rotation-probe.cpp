#include "captureservice.h"
#include "mainwindow.h"
#include "playeractions.h"
#include "playlisthost.h"
#include "sessioncontroller.h"
#include "videosurface.h"
#include "videowidget.h"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QScreen>
#include <QScopeGuard>
#include <QSemaphore>
#include <QSignalSpy>
#include <QSplitter>
#include <QTest>
#include <QTabWidget>
#include <QThreadPool>
#include <QTransform>
#include <QWindow>
#include <QtConcurrentRun>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <array>
#include <clocale>
#include <cmath>
#include <cstdio>
#include <memory>
#include <stdexcept>

namespace {
constexpr std::array angles{0, 90, 180, 270};
const QString cornerOrders[]{"RGBY", "BRYG", "YBGR", "GYRB"};

QJsonArray dimensions(QSize size) { return {size.width(), size.height()}; }
QJsonArray rectangle(QRect rect) { return {rect.x(), rect.y(), rect.width(), rect.height()}; }

bool until(auto predicate, int timeout = 6000)
{
    QElapsedTimer clock;
    clock.start();
    bool ready = predicate();
    while (!ready && clock.elapsed() < timeout) {
        QTest::qWait(15);
        ready = predicate();
    }
    return ready;
}

QRect pictureBounds(const QImage &image)
{
    if (image.isNull()) return {};
    const QPoint center = image.rect().center();
    const auto lit = [&](int x, int y) {
        const auto color = image.pixel(x, y);
        return qRed(color) + qGreen(color) + qBlue(color) > 50;
    };
    int left = 0, right = image.width() - 1, top = 0, bottom = image.height() - 1;
    while (left <= right && !lit(left, center.y())) ++left;
    while (right >= left && !lit(right, center.y())) --right;
    while (top <= bottom && !lit(center.x(), top)) ++top;
    while (bottom >= top && !lit(center.x(), bottom)) --bottom;
    if (left > right || top > bottom) return {};
    return QRect(QPoint(left, top), QPoint(right, bottom));
}

QString corners(const QImage &image)
{
    const auto rect = pictureBounds(image);
    if (rect.isEmpty()) return {};
    const std::array colors{qRgb(232,44,53), qRgb(40,217,103), qRgb(48,108,235), qRgb(239,202,50)};
    QString order;
    for (double v : {.045, .955}) {
        for (double u : {.045, .955}) {
            const auto pixel = image.pixel(rect.x() + qRound((rect.width()-1)*u), rect.y() + qRound((rect.height()-1)*v));
            int best = 100 * 100;
            char label = '?';
            for (size_t i = 0; i < colors.size(); ++i) {
                const auto target = colors[i];
                const int r = qRed(pixel)-qRed(target), g = qGreen(pixel)-qGreen(target), b = qBlue(pixel)-qBlue(target);
                const int score = r*r + g*g + b*b;
                if (score < best) { best = score; label = "RGBY"[i]; }
            }
            order.append(QLatin1Char(label));
        }
    }
    return order;
}

QRect changedPixels(const QImage &before, const QImage &after, int threshold=30)
{
    if (before.size() != after.size()) return {};
    int left = after.width(), right = -1, top = after.height(), bottom = -1;
    for (int y = 0; y < after.height(); ++y) {
        const auto *a = reinterpret_cast<const QRgb *>(before.constScanLine(y));
        const auto *b = reinterpret_cast<const QRgb *>(after.constScanLine(y));
        for (int x = 0; x < after.width(); ++x) {
            if (std::abs(qRed(a[x])-qRed(b[x])) + std::abs(qGreen(a[x])-qGreen(b[x])) + std::abs(qBlue(a[x])-qBlue(b[x])) <= threshold) continue;
            left = std::min(left,x); right = std::max(right,x);
            top = std::min(top,y); bottom = y;
        }
    }
    if (left > right || top > bottom) return {};
    return QRect(QPoint(left,top),QPoint(right,bottom));
}

Player &initialize(Player &player)
{
    if (!player.initialize()) throw std::runtime_error("initialize libmpv");
    return player;
}

struct Sample {
    const char *file;
    QSize unrotated;
    int metadataRotation = 0;
};

class Probe
{
public:
    Probe(QDir fixtures, QDir output, QJsonObject &report)
        : window_(initialize(player_)), settings_(output.filePath("config")), history_(output.filePath("data")),
          session_(player_, window_, settings_, history_), observer_(mpv_create_client(player_.handle(),"rotation-probe"),mpv_destroy),
          fixtures_(std::move(fixtures)), output_(std::move(output)), report_(report)
    {
        require(observer_ != nullptr, "event observer created");
        QObject::connect(&player_, &Player::videoSizeChanged, &window_, [this](QSize size) { display_ = size; });
        QObject::connect(&player_, &Player::mediaChanged, &window_, [this](auto state) {
            if (state == Player::State::Loading && !player_.lastLoadWasRecovery()) player_.setInitialPaused(true);
        });
        player_.setMuted(true);
        window_.resize(1120, 760);
        window_.show(); window_.raise(); window_.activateWindow();
        require(until([&] { return video_->rendererReady(); }), "OpenGL renderer created");
        require(QTest::qWaitForWindowActive(&window_), "window accepts viewing shortcuts");
        dpr_ = window_.windowHandle()->devicePixelRatio();
        report_["qt"] = qVersion();
        report_["mpv"] = property("mpv-version").toString();
        report_["dpr"] = dpr_;
        report_["widgetDpr"] = video_->devicePixelRatioF();
        report_["screen"] = window_.screen()->name();
        report_["availableGeometryDip"] = rectangle(window_.screen()->availableGeometry());
        report_["qtAvailableScreens"] = int(QGuiApplication::screens().size());
    }

    ~Probe() { window_.close(); player_.shutdown(); }

    void run(double expectedDpr)
    {
        require(dpr_ == expectedDpr && video_->devicePixelRatioF() == dpr_, "window and widget DPR match the requested sample");
        const Sample grid{"grid.mp4",{960,540}}, sar{"sar.mp4",{960,540}}, rotated{"sar-90.mp4",{960,540},90};
        const Sample square{"square.mp4",{722,722}}, wide{"wide-1080.mp4",{1920,1080}};
        for (const auto &sample : {grid, sar, rotated, square}) rotationMatrix(sample);
        subtitles(rotated);
        takeView(rotated);
        pendingCaptures(grid);
        rapidRotation(rotated);
        playingRotation(rotated);
        sizing(wide, sar, rotated, square);
        policies(wide, rotated);
        rendererRecovery(rotated);
        writePreferences(wide);
    }

private:
    static void require(bool passed, const QString &description)
    {
        std::printf("%s %s\n", passed ? "PASS" : "FAIL", qPrintable(description));
        std::fflush(stdout);
        if (!passed) throw std::runtime_error(description.toStdString());
    }

    void record(const char *group, QJsonObject data)
    {
        auto rows = report_[group].toArray();
        rows.append(data);
        report_[group] = rows;
    }

    QVariant property(const char *name) const
    {
        mpv_node value{};
        if (mpv_get_property(observer_.get(),name,MPV_FORMAT_NODE,&value) < 0) return {};
        const auto converted = nodeValue(value);
        mpv_free_node_contents(&value);
        return converted;
    }

    void set(const char *name, const QByteArray &value) const
    {
        const int result = mpv_set_property_string(observer_.get(),name,value.constData());
        if (result < 0) throw std::runtime_error(QString("set %1: %2").arg(name,mpv_error_string(result)).toStdString());
    }

    QJsonObject events()
    {
        QJsonObject counts;
        for (;;) {
            const auto id = mpv_wait_event(observer_.get(),0)->event_id;
            if (id == MPV_EVENT_NONE) break;
            if (id == MPV_EVENT_SEEK || id == MPV_EVENT_START_FILE || id == MPV_EVENT_END_FILE
                || id == MPV_EVENT_VIDEO_RECONFIG || id == MPV_EVENT_PLAYBACK_RESTART) {
                const QString key = QString::fromLatin1(mpv_event_name(id));
                counts[key] = counts[key].toInt()+1;
            }
        }
        return counts;
    }

    QImage frame() const { return video_->grabFramebuffer().convertToFormat(QImage::Format_RGB32); }

    QAction *action(PlayerActions::Id id) const { return window_.findChild<PlayerActions *>()->get(id); }

    void key(Qt::Key key, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        if (!window_.isActiveWindow()) { window_.raise(); window_.activateWindow(); }
        if (!QTest::qWaitForWindowActive(&window_))
            throw std::runtime_error("viewing shortcut requires the active window");
        video_->setFocus();
        if (!until([&] { return QApplication::focusWidget() == video_; }))
            throw std::runtime_error("viewing shortcut requires focus on the video");
        QTest::keyClick(video_, key, modifiers);
    }

    void open(const Sample &sample)
    {
        player_.setMediaProperty("video-rotate", 0);
        video_->resetView();
        display_ = {};
        player_.openFile(fixtures_.filePath(sample.file),42);
        require(until([&] {
            return player_.canSeek() && player_.paused() && video_->canCaptureTransform(player_.videoTransformVersion()) && !display_.isEmpty()
                && corners(frame()) == cornerOrders[sample.metadataRotation/90];
        }), QString("%1 begins paused in its metadata orientation").arg(sample.file));
        player_.seek(2.4);
        require(until([&] { return std::abs(player_.position()-2.4)<.04 && !property("seeking").toBool(); }), "paused position ready");
        QTest::qWait(80);
    }

    QImage rotate(const Sample &sample, int angle, bool checkCorners = true)
    {
        const int expected = (sample.metadataRotation+angle)%360;
        QSize size = sample.unrotated;
        if (expected%180) size.transpose();
        const auto generation = player_.generation();
        const auto epoch = player_.renderEpoch();
        const auto pos = player_.position();
        const bool paused = player_.paused();
        const auto aid = property("aid"), sid = property("sid"), speed = property("speed");
        const auto a = property("ab-loop-a"), b = property("ab-loop-b");
        auto *context = video_->context();
        QSignalSpy hostCommands(&player_,&Player::requestSubmitted);
        int paramsMs = -1;
        QSignalSpy paramsChanged(&player_,&Player::videoSizeChanged), swaps(video_,&QOpenGLWidget::frameSwapped);
        (void)events();
        QElapsedTimer elapsed;
        elapsed.start();
        player_.setMediaProperty("video-rotate", angle);
        QImage image;
        const bool ready = until([&] {
            if (!player_.videoTransformReady() || player_.videoRotation() != expected || display_ != size) return false;
            if (paramsMs < 0) paramsMs = int(elapsed.elapsed());
            image = frame();
            return video_->canCaptureTransform(player_.videoTransformVersion()) && !image.isNull()
                && (!checkCorners || corners(image)==cornerOrders[expected/90]);
        });
        const auto frameMs = elapsed.elapsed();
        QTest::qWait(65);
        const auto engineEvents = events();
        const auto params = property("video-out-params").toMap();
        int hostSeeks = 0, hostLoads = 0;
        for (const auto &command : hostCommands) {
            const auto operation = command.at(3).toString();
            hostSeeks += operation.startsWith("seek ");
            hostLoads += operation.startsWith("loadfile ");
        }
        record("rotations", {{"file",sample.file},{"user",angle},{"metadata",sample.metadataRotation},{"expectedRotation",expected},
            {"voParams",QJsonObject::fromVariantMap(params)},{"expectedPixels",dimensions(size)},{"playerPixels",dimensions(display_)},
            {"paramsMs",paramsMs},{"firstMatchingFrameMs",frameMs},{"videoSizeNotifications",paramsChanged.size()},{"frameSwaps",swaps.size()},
            {"screenOrder",checkCorners ? corners(image) : QString()},
            {"pausedBefore",paused},{"pausedAfter",player_.paused()},{"positionBefore",pos},{"positionAfter",player_.position()},
            {"engineEvents",engineEvents},{"hostSeeks",hostSeeks},{"hostLoads",hostLoads}});
        require(ready, QString("%1 @ %2 deg effective size and rendered direction").arg(sample.file).arg(angle));
        require(hostSeeks == 0 && hostLoads == 0 && engineEvents["start-file"].toInt() == 0
            && engineEvents["end-file"].toInt() == 0 && player_.generation() == generation && player_.renderEpoch() == epoch
            && video_->context() == context && player_.paused() == paused && player_.itemId() == 42
            && (!paused || std::abs(player_.position()-pos)<.04)
            && property("aid")==aid && property("sid")==sid && property("speed")==speed
            && property("ab-loop-a")==a && property("ab-loop-b")==b, "rotation preserves session and playback parameters");
        return frame();
    }

    QImage capture(const QString &name, bool source)
    {
        const QFileInfo file(output_.filePath(name));
        captures_->setDirectory(file.absolutePath());
        QSignalSpy saved(captures_,&CaptureService::saved);
        QSignalSpy failed(captures_,&CaptureService::failed);
        if (source) captures_->source(); else captures_->viewport();
        const bool finished = until([&] { return !saved.isEmpty() || !failed.isEmpty(); });
        require(finished && saved.size()==1 && failed.isEmpty(), QString("production PNG saved for %1%2")
            .arg(name, failed.isEmpty() ? QString() : QString(" (%1)").arg(failed.first().first().toString())));
        require(saved.first()[1].toBool() && QFile::rename(saved.first()[0].toString(),file.absoluteFilePath()), "capture written on worker and retained in evidence");
        return QImage(file.absoluteFilePath()).convertToFormat(QImage::Format_RGB32);
    }

    QImage compareCapture(const QString &folder, const QImage &screen)
    {
        surface_->showOsd("probe",QStringLiteral("Rotation sample OSD"),5000);
        const auto view = capture(folder+"/viewport.png",false);
        const auto sampled = frame();
        const auto full = capture(folder+"/full.png",true);
        const bool exact = sampled == view;
        record("framebufferComparisons",{{"folder",folder},{"exact",exact}});
        require(sampled.size() == view.size() && changedPixels(sampled,view,3).isEmpty()
            && pictureBounds(screen) == pictureBounds(view)
            && view.size() == QSize(qRound(video_->width()*dpr_),qRound(video_->height()*dpr_)),
            "viewport PNG matches the physical framebuffer within RGB rounding and excludes the Qt OSD");
        surface_->dismissOsd();
        return full;
    }

    QString folder(const Sample &sample, int angle) const
    {
        return QFileInfo(sample.file).completeBaseName()+QString("-%1").arg(angle,3,10,QLatin1Char('0'));
    }

    void rotationMatrix(const Sample &sample)
    {
        open(sample);
        QImage baseline;
        for (int angle : angles) {
            const auto screen = rotate(sample,angle);
            const auto path = folder(sample,angle);
            const auto full = compareCapture(path,screen);
            const int displayAngle = (sample.metadataRotation+angle)%360;
            const auto unrotated = full.transformed(QTransform().rotate(-displayAngle));
            if (baseline.isNull()) baseline = unrotated;
            record("captures",{{"file",sample.file},{"user",angle},{"fullSize",dimensions(full.size())},{"fullOrder",corners(full)},
                {"viewportSize",dimensions(screen.size())},{"pictureBounds",rectangle(pictureBounds(screen))},{"folder",path}});
            require(unrotated.size() == sample.unrotated && unrotated == baseline && corners(full)==corners(screen),
                "full-frame PNG has the confirmed direction and SAR without the viewport crop");
        }
    }

    void subtitles(const Sample &sample)
    {
        open(sample);
        for (const auto *name : {"external.srt","external.ass","legacy.ssa"}) {
            const QString path = fixtures_.filePath(name);
            player_.addSubtitle(path);
            require(until([&] {
                const auto sid = property("sid").toLongLong();
                return std::ranges::any_of(player_.tracks(),[&](const MediaTrack &track) {
                    return track.type=="sub" && track.id==sid && track.external && mediaPathKey(track.externalPath)==mediaPathKey(path);
                });
            }), QString("selected %1").arg(name));
            for (int angle : angles) {
                set("sub-visibility","no");
                const auto noSubs = rotate(sample,angle);
                const auto path = folder(sample,angle);
                const QImage clean(output_.filePath(path+"/viewport.png"));
                require(noSubs.size() == clean.size() && changedPixels(noSubs,clean.convertToFormat(QImage::Format_RGB32),3).isEmpty(),
                    "hidden subtitles match the track-free framebuffer");
                set("sub-visibility","yes");
                QImage screen;
                QRect text;
                require(until([&] {
                    screen=frame(); text=changedPixels(noSubs,screen);
                    return !text.isEmpty();
                }), "paused subtitle changes redraw in place");
                const auto shotPath = path+"-"+QFileInfo(name).suffix();
                const auto full = compareCapture(shotPath,screen);
                record("subtitles",{{"type",QFileInfo(name).suffix()},{"user",angle},{"textBounds",rectangle(text)},
                    {"pictureBounds",rectangle(pictureBounds(screen))},{"folder",shotPath}});
                require(text.width()>2*text.height() && text.center().y()>pictureBounds(screen).center().y()
                    && full == QImage(output_.filePath(path+"/full.png")).convertToFormat(QImage::Format_RGB32),
                    "subtitle remains horizontal and below center; full-frame PNG excludes it");
            }
        }
        set("sub-visibility","yes");
    }

    void takeView(const Sample &sample)
    {
        open(sample);
        const QPointF focus(.17,.12);
        for (double zoom : {2.,8.}) {
            video_->restoreView(zoom,focus);
            for (int angle : {0,90}) {
                const auto screen = rotate(sample,angle,false);
                const auto path = folder(sample,angle)+QString("-zoom-%1").arg(zoom);
                const auto full = compareCapture(path,screen);
                require(video_->zoomScale()==zoom && (video_->viewFocus()-focus).manhattanLength()<.00001
                    && full == QImage(output_.filePath(folder(sample,angle)+"/full.png")).convertToFormat(QImage::Format_RGB32),
                    "rotation keeps crop state and the complete picture remains uncropped");
                const auto fitted = QImage(output_.filePath(folder(sample,angle)+"/viewport.png")).convertToFormat(QImage::Format_RGB32);
                require(screen.size() == fitted.size() && !changedPixels(screen,fitted).isEmpty(),
                    "zoomed viewport actually differs from fit-to-window");
                const QPointF mapped = angle==0 ? QPointF(1-focus.y(),focus.x()) : QPointF(1-focus.x(),1-focus.y());
                ViewGeometry expected;
                expected.setSizes(video_->size(),display_);
                expected.setFocus(zoom,mapped);
                const auto size = expected.frameSize();
                const auto point = expected.topLeft()+QPointF(mapped.x()*size.width(),mapped.y()*size.height());
                const QPoint pixel(qRound(point.x()*dpr_),qRound(point.y()*dpr_));
                require(screen.rect().contains(pixel), "focused point stays visible at the crop boundary");
                const auto redMarker = screen.pixel(pixel);
                record("zoom",{{"user",angle},{"zoom",zoom},{"focus",QJsonArray{focus.x(),focus.y()}},
                    {"mappedPointPx",QJsonArray{pixel.x(),pixel.y()}},{"rgb",QJsonArray{qRed(redMarker),qGreen(redMarker),qBlue(redMarker)}},
                    {"folder",path}});
                require(qRed(redMarker)>180 && qGreen(redMarker)<100 && qBlue(redMarker)<120,
                    "the saved content point still hits the same red marker after rotation");
            }
        }
        key(Qt::Key_R);
        require(video_->zoomScale()==1, "R restores the view to fit at 1x");
        require(until([&] { return player_.videoRotation()==180 && corners(frame())==cornerOrders[2]; }),
            "view reset keeps the current rotation");
        video_->restoreView(2, focus);
        key(Qt::Key_K, Qt::AltModifier | Qt::ControlModifier);
        require(until([&] { return player_.videoTransformReady() && player_.videoRotation() == sample.metadataRotation; })
            && video_->zoomScale() == 2
            && (video_->viewFocus()-focus).manhattanLength() < .00001, "rotation reset preserves the chosen zoom and content point");
    }

    void pendingCaptures(const Sample &sample)
    {
        open(sample);
        auto *pool = QThreadPool::globalInstance();
        const int oldLimit = pool->maxThreadCount();
        pool->setMaxThreadCount(1);
        QSemaphore entered, release;
        auto blocker = QtConcurrent::run([&] { entered.release(); release.acquire(); });
        auto unblock = qScopeGuard([&] { release.release(); blocker.waitForFinished(); pool->setMaxThreadCount(oldLimit); });
        require(entered.tryAcquire(1,1000), "capture worker barrier ready");
        const auto generation = player_.generation();
        const auto epoch = player_.renderEpoch();
        captures_->setDirectory(output_.filePath("pending"));
        QSignalSpy raw(&player_,&Player::sourceImageReady), saved(captures_,&CaptureService::saved);
        captures_->viewport(); captures_->source();
        require(until([&] { return raw.size()==1; }), "source PNG queued before transform changes");
        rotate(sample,90);
        require(saved.isEmpty(), "both PNG writes still pending on new orientation");
        release.release(); blocker.waitForFinished(); pool->setMaxThreadCount(oldLimit); unblock.dismiss();
        pool->waitForDone();
        const auto pending = output_.filePath("pending");
        require(until([&] { return QDir(pending).entryList(QDir::Files | QDir::Hidden).isEmpty(); }) && saved.isEmpty(),
            "both stale PNGs are discarded before publishing a file or success notification");
        raw.clear();
        captures_->source();
        player_.setMediaProperty("video-rotate", 180);
        require(until([&] { return player_.videoTransformReady() && player_.videoRotation() == 180; }) && raw.isEmpty(),
            "old engine screenshot reply is rejected after a newer target");
        record("pendingCaptures",{{"oldAngle",0},{"newAngle",90},{"returned",saved.size()},{"sameGeneration",player_.generation()==generation},
            {"sameRenderEpoch",player_.renderEpoch()==epoch}});
    }

    void playingRotation(const Sample &sample)
    {
        open(sample);
        set("speed","1.25");
        set("ab-loop-a","2");
        set("ab-loop-b","6");
        player_.setPaused(false);
        require(until([&] { return !player_.paused() && player_.position()>=2.65; }), "playback advances before rotation");
        rotate(sample,90);
        require(player_.position()>2.65 && player_.loopActive(), "rotation during playback preserves playback progress and A–B");
        player_.setPaused(true);
        require(until([&] { return player_.paused(); }), "playback paused again");
        set("speed","1");
    }

    void rapidRotation(const Sample &sample)
    {
        open(sample);
        const auto generation = player_.generation();
        (void)events();
        for (int i = 0; i < 3; ++i) key(Qt::Key_K, Qt::AltModifier);
        require(until([&] { return player_.videoRotation()==0 && display_==sample.unrotated && corners(frame())==cornerOrders[0]; }),
            "rapid ordered writes settle to the newest angle and displayed frame");
        require(player_.generation()==generation && player_.paused() && std::abs(player_.position()-2.4)<.04,
            "burst keeps generation, pause and position");
        key(Qt::Key_K, Qt::AltModifier | Qt::ShiftModifier);
        require(until([&] { return player_.videoRotation() == 270 && player_.mediaProperty("video-rotate").toInt() == 180; }),
            "counterclockwise shortcut applies to the latest angle");
        record("burst",{{"writes",QJsonArray{90,180,270}},{"effective",player_.videoRotation()},{"events",events()}});
    }

    void windowSample(const Sample &sample, double scale, const QString &layout)
    {
        const auto work = window_.screen()->availableGeometry();
        const auto margins = window_.windowHandle()->frameMargins();
        const QSize frameExtra(margins.left()+margins.right(),margins.top()+margins.bottom());
        auto *host = window_.findChild<PlaylistHost *>();
        const int dockWidth = layout=="docked" ? host->dockExtent() : 0;
        const QSize chrome(dockWidth, window_.height()-video_->height());
        const auto maxClient = work.size()-frameExtra;
        const auto maxView = maxClient-chrome;
        const auto ideal = QSizeF(display_)*scale/dpr_;
        const double fit = scale==0 ? 1. : std::min(maxView.width()/ideal.width(),maxView.height()/ideal.height());
        const double factor = scale==0 ? 0 : std::min(1.,fit);
        const auto targetView = scale==0 ? display_.scaled(maxView,Qt::KeepAspectRatio) : (ideal*factor).toSize();
        auto minClient = window_.minimumSize();
        if (layout=="docked") minClient.setWidth(std::max(host->dockThreshold(),minClient.width()));
        const auto requested = (targetView+chrome).expandedTo(minClient).boundedTo(maxClient);
        QSignalSpy commands(&player_, &Player::requestSubmitted);
        const auto generation = player_.generation();
        const int number = scale == 0 ? 5 : qRound(scale * 2);
        key(Qt::Key(Qt::Key_0 + number), layout == "detached" ? Qt::KeypadModifier : Qt::NoModifier);
        const bool resized = until([&] { return window_.sizePolicy() == (scale ? WindowSizePolicy::VideoScale : WindowSizePolicy::FitWorkArea)
            && (!scale || window_.lastVideoScale() == scale) && std::abs(window_.width() - requested.width())
                + std::abs(window_.height() - requested.height()) <= 2; });
        require(resized, QString("window shortcut %1 applies production geometry for %2 / %3 (%4x%5 vs %6x%7, policy=%8, ratio=%9)")
            .arg(number).arg(sample.file, layout).arg(window_.width()).arg(window_.height()).arg(requested.width()).arg(requested.height())
            .arg(int(window_.sizePolicy())).arg(window_.lastVideoScale()));
        QTest::qWait(60);
        const auto screen = frame();
        const auto picture = pictureBounds(screen);
        const auto targetPx = QSizeF(display_)*scale;
        const bool minLimited = scale>0 && fit>=1 && (std::abs(double(picture.width())-targetPx.width())>2
            || std::abs(double(picture.height())-targetPx.height())>2)
            && requested != targetView+chrome;
        const auto sheetPath = QString("window-%1-%2-%3.png").arg(QFileInfo(sample.file).completeBaseName(),layout).arg(scale);
        require(screen.save(output_.filePath(sheetPath)), "save physical viewport for sizing sample");
        record("windows",{{"media",sample.file},{"layout",layout},{"scale",scale},{"displayPx",dimensions(display_)},
            {"targetPicturePx",QJsonArray{targetPx.width(),targetPx.height()}},{"targetVideoDip",dimensions(targetView)},
            {"chromeDip",dimensions(chrome)},{"frameExtraDip",dimensions(frameExtra)},{"workAreaDip",rectangle(work)},
            {"clientDip",rectangle(window_.geometry())},{"windowDip",rectangle(window_.frameGeometry())},{"viewDip",dimensions(video_->size())},
            {"viewportPx",dimensions(screen.size())},{"picturePx",rectangle(picture)},{"workAreaLimited",scale==0 || fit<1},
            {"minimumLimited",minLimited},{"image",sheetPath}});
        require(work.contains(window_.frameGeometry()) && !picture.isEmpty(), "resized native frame fits the available work area");
        if (scale>0 && fit>=1 && !minLimited)
            require(std::abs(picture.width()-targetPx.width())<=2 && std::abs(picture.height()-targetPx.height())<=2,
                "unconstrained visible picture is within two physical pixels per axis");
        if (scale > 0 && (fit < 1 || minLimited)) {
            auto *osd = window_.findChild<QLabel *>("playerOsd");
            require(osd->isVisible() && osd->text().contains(QStringLiteral("实际")), "restricted size reports the actual scale");
        }
        require(player_.generation() == generation && player_.paused() && std::ranges::none_of(commands, [](const auto &command) {
            const auto op = command.at(3).toString();
            return op.startsWith("seek ") || op.startsWith("loadfile ");
        }), "window sizing leaves playback and engine position intact");
        const auto fromWidth = double(picture.width())*display_.height()/display_.width();
        const auto fromHeight = double(picture.height())*display_.width()/display_.height();
        require(std::min(std::abs(fromWidth-picture.height()),std::abs(fromHeight-picture.width()))<=2,
            "sizing preserves the effective display aspect ratio");
    }

    void sizing(const Sample &wide, const Sample &sar, const Sample &rotated, const Sample &square)
    {
        auto *host = window_.findChild<PlaylistHost *>();
        for (const auto &sample : {wide,sar,rotated,square}) {
            window_.setWindowSizing(WindowSizePolicy::LastGeometry, 1);
            open(sample);
            for (double scale : {.5,1.,1.5,2.}) windowSample(sample,scale,"hidden");
        }
        open(rotated); windowSample(rotated,0,"fit");
        const Sample ultra{"wide-2160.mp4",{3840,2160}}, lowResolution{"small.mp4",{320,180}};
        open(ultra); windowSample(ultra,2,"hidden");
        open(lowResolution); windowSample(lowResolution,.5,"hidden");
        host->setVisible(true,false);
        open(wide); windowSample(wide,1,"docked");
        require(window_.width()-video_->width()>250 && !host->overlayVisible(), "docked list takes client width");
        host->setDetached(true);
        QTest::qWait(80);
        windowSample(wide,1,"detached");
        require(window_.width()==video_->width() && host->window()->isVisible(), "detached list takes no client width");
        host->setDetached(false);
        open(sar);
        windowSample(sar,.5,"overlay");
        const auto shot = frame();
        require(shot.save(output_.filePath("window-overlay.png")), "save overlay viewport");
        record("overlay",{{"windowDip",rectangle(window_.frameGeometry())},{"viewDip",dimensions(video_->size())},
            {"viewportPx",dimensions(shot.size())},{"picturePx",rectangle(pictureBounds(shot))}});
        require(window_.width()==video_->width() && host->overlayVisible(), "overlay list takes no client width");
        const auto overlayWindow = window_.frameGeometry();
        const auto overlayView = video_->size();
        QTest::qWait(100);
        require(host->overlayVisible() && window_.frameGeometry()==overlayWindow && video_->size()==overlayView,
            "overlay settles without an extra layout transition");
        host->setVisible(false,false);
        window_.setWindowSizing(WindowSizePolicy::LastGeometry, 1);
        const auto ordinary = window_.windowedGeometry();
        const auto generation = player_.generation();
        auto *fullscreen = window_.findChild<PlayerActions *>()->get(PlayerActions::Fullscreen);
        fullscreen->trigger();
        require(until([&] { return window_.isFullScreen(); }) && window_.windowedGeometry()==ordinary,
            "fullscreen retains the ordinary rectangle for ratio-point restore");
        fullscreen->trigger();
        require(until([&] { return !window_.isFullScreen() && window_.windowedGeometry()==ordinary; })
            && player_.generation()==generation && player_.paused(), "leaving fullscreen retains ordinary geometry and paused media");

        player_.openFile(fixtures_.filePath("audio.m4a"),42);
        require(until([&] { return player_.canSeek() && player_.paused() && player_.path().endsWith("audio.m4a"); })
            && property("video-out-params").toMap().isEmpty() && !video_->canCapture() && !action(PlayerActions::Scale100)->isEnabled()
            && !action(PlayerActions::RotateNext)->isEnabled(), "audio-only media has no ratio or rotation command");
    }

    void policies(const Sample &wide, const Sample &rotated)
    {
        open(wide);
        const auto ordinary = window_.geometry();
        const auto generation = player_.generation();
        const QPointF focus(.33, .67);
        video_->restoreView(2, focus);
        action(PlayerActions::Fullscreen)->trigger();
        require(until([&] { return window_.isFullScreen(); }), "fullscreen entered for explicit sizing");
        key(Qt::Key_2);
        require(until([&] { return !window_.isFullScreen() && !window_.isMaximized() && window_.sizePolicy() == WindowSizePolicy::VideoScale; }),
            "100 percent exits fullscreen to an ordinary window");
        window_.showMaximized();
        require(until([&] { return window_.isMaximized(); }), "maximized before fit command");
        key(Qt::Key_5);
        require(until([&] { return !window_.isMaximized() && window_.sizePolicy() == WindowSizePolicy::FitWorkArea; })
            && player_.generation() == generation && player_.paused() && video_->zoomScale() == 2 && video_->viewFocus() == focus,
            "fit leaves maximized state and preserves playback and zoom");
        QTest::qWait(100);
        const auto fit = window_.frameGeometry();
        window_.resize(window_.width() - 12, window_.height() - 10);
        require(window_.sizePolicy() == WindowSizePolicy::FitWorkArea, "programmatic resize is independent of native manual sizing");
        const auto hwnd = reinterpret_cast<HWND>(window_.winId());
        RECT rect{};
        GetWindowRect(hwnd, &rect);
        SendMessageW(hwnd, WM_SIZING, WMSZ_BOTTOMRIGHT, reinterpret_cast<LPARAM>(&rect));
        require(window_.sizePolicy() == WindowSizePolicy::LastGeometry && window_.lastVideoScale() == 1,
            "native WM_SIZING restores ordinary-window policy and retains the last scale");
        open(rotated);
        require(window_.frameGeometry() != fit && window_.windowedGeometry() != ordinary,
            "a later file preserves the user-sized window under its ordinary policy");

        const auto smallWindow = solveVideoWindow({3840,2160},1.5,2,QRect(4000,80,880,620),QRect(5200,0,700,500),
            QMargins(8,8,8,8),QSize(640,400),130,325,900);
        require(!smallWindow.docked && QRect(4000,80,880,620).contains(smallWindow.client.marginsAdded(QMargins(8,8,8,8))),
            "small secondary work area clamps a 4K request and selects an overlay");
        const auto large = solveVideoWindow({3840,2160},2,0,QRect(0,0,1920,1040),QRect(100,100,1100,720),
            {},QSize(640,400),130,325,900);
        require(large.docked && QRect(0,0,1920,1040).contains(large.client), "fit includes the dock where the 4K sample can support it");
        record("syntheticDisplays", {{"small", rectangle(smallWindow.client)}, {"wide",rectangle(large.client)}});
    }

    void rendererRecovery(const Sample &sample)
    {
        open(sample);
        video_->restoreView(2, {.27,.62});
        rotate(sample, 90, false);
        const auto position = player_.position();
        const auto generation = player_.generation();
        const auto epoch = player_.renderEpoch();
        for (int cycle = 0; cycle < 3; ++cycle) {
            QSignalSpy restored(&player_, &Player::restored);
            player_.setRendererReady(false);
            player_.setRendererReady(true);
            require(until([&] { return restored.count() == 1 && player_.paused() && player_.videoTransformReady()
                && video_->canCaptureTransform(player_.videoTransformVersion()) && player_.videoRotation() == 180; }),
                "renderer recovery restores orientation before the confirmed frame");
        }
        require(player_.generation() == generation && player_.renderEpoch() == epoch + 3
            && std::abs(player_.position()-position) < .05 && video_->zoomScale() == 2
            && (video_->viewFocus() - QPointF(.27,.62)).manhattanLength() < .00001,
            "repeated recovery preserves generation, pause, position, scale and content point");
    }

    void writePreferences(const Sample &sample)
    {
        open(sample);
        rotate(sample, 270);
        video_->restoreView(2, {.31,.64});
        key(Qt::Key_2);
        require(until([&] { return window_.sizePolicy() == WindowSizePolicy::VideoScale; }), "saved window policy set by viewing shortcut");
        require(video_->zoomScale() == 2 && (video_->viewFocus() - QPointF(.31,.64)).manhattanLength() < .00001,
            "the window policy change preserves the current crop");
        session_.clearHistory();
        window_.close();
        const auto p = settings_.load();
        require(!p.recordHistory && p.userRotationDeg == 270 && p.sizePolicy == WindowSizePolicy::VideoScale && p.lastVideoScale == 1,
            "rotation and window policy persist with history disabled");
    }

public:
    void readPreferences()
    {
        const auto p = session_.preferences();
        require(player_.path().isEmpty() && !p.recordHistory && p.userRotationDeg == 270
            && window_.sizePolicy() == WindowSizePolicy::VideoScale && window_.lastVideoScale() == 1,
            "a fresh process restores preferences and starts without a media item");
        player_.openFile(fixtures_.filePath("sar-90.mp4"),42);
        require(until([&] { return player_.paused() && player_.videoTransformReady() && player_.videoRotation() == 0
            && video_->canCaptureTransform(player_.videoTransformVersion()); }), "new file combines saved rotation with its own metadata");
        const auto expected = solveVideoWindow(display_, dpr_, 1, window_.screen()->availableGeometry(), window_.frameGeometry(),
            window_.windowHandle()->frameMargins(), window_.minimumSize(), window_.height()-video_->height(), 0, 900);
        require(!display_.isEmpty() && until([&] { return window_.size() == expected.client.size(); }),
            "saved 100 percent policy sizes the ordinary window after effective dimensions arrive");
        require(video_->zoomScale() == 1 && video_->viewFocus() == QPointF(.5,.5),
            "the first video starts with a centered view despite the previous crop");
        video_->restoreView(2, {.31,.64});
        auto *settings = action(PlayerActions::Settings);
        QTimer::singleShot(120, &window_, [this] {
            auto *dialog = window_.findChild<QDialog *>("settingsDialog");
            if (!dialog) return;
            auto *tabs = dialog->findChild<QTabWidget *>("settingsTabs");
            tabs->setCurrentWidget(tabs->findChild<QWidget *>("videoSettingsPage"));
            dialog->findChild<QComboBox *>("videoRotationCombo")->setCurrentIndex(1);
            dialog->findChild<QCheckBox *>("resetViewOnOpenCheck")->setChecked(true);
            dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
            if (dialog->isVisible()) dialog->reject();
        });
        settings->trigger();
        require(until([&] { return player_.videoRotation() == 180; }) && video_->zoomScale() == 2,
            "settings apply the additional angle immediately and defer the crop reset");
        player_.openFile(fixtures_.filePath("square.mp4"),42);
        require(until([&] { return player_.paused() && player_.videoTransformReady() && player_.videoRotation() == 90
            && video_->zoomScale() == 1 && video_->viewFocus() == QPointF(.5,.5); }),
            "new-file crop reset preserves the independently saved rotation");
    }

private:

    Player player_;
    MainWindow window_;
    VideoWidget *video_ = window_.findChild<VideoWidget *>();
    VideoSurface *surface_ = window_.findChild<VideoSurface *>();
    CaptureService *captures_ = window_.findChild<CaptureService *>();
    SettingsStore settings_;
    HistoryStore history_;
    SessionController session_;
    std::unique_ptr<mpv_handle,decltype(&mpv_destroy)> observer_;
    QDir fixtures_, output_;
    QJsonObject &report_;
    QSize display_;
    double dpr_ = 0;
};
}

int main(int argc, char **argv)
{
    QApplication app(argc,argv);
    app.setApplicationName("zopana rotation probe");
    std::setlocale(LC_NUMERIC,"C");
    const auto args = app.arguments();
    if (args.size()!=4 && args.size()!=5) return 2;
    const QDir output(args[2]);
    if (!QDir().mkpath(output.absolutePath())) return 2;
    QJsonObject report;
    bool passed = false;
    try {
        Probe probe(QDir(args[1]),output,report);
        if (args.value(4) == "--read") probe.readPreferences();
        else probe.run(args[3].toDouble());
        passed=true;
    } catch (const std::exception &error) {
        report["failure"] = QString::fromUtf8(error.what());
        std::fprintf(stderr,"%s\n",error.what());
    }
    report["passed"] = passed;
    QFile evidence(output.filePath(args.value(4) == "--read" ? "restart.json" : "results.json"));
    if (!evidence.open(QIODevice::WriteOnly)) return 2;
    evidence.write(QJsonDocument(report).toJson());
    return passed ? 0 : 1;
}
