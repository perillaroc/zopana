#include "mediacontrols.h"
#include "player.h"
#include "playeractions.h"
#include "videowidget.h"
#include "videosurface.h"
#include "controlbar.h"
#include "captureservice.h"
#include "playbackdialogs.h"
#include <QAction>
#include <QApplication>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
#include <QPointer>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidgetAction>
#include <cmath>
#include <tuple>

namespace {
QWidget *owner(QMainWindow &window) { return QApplication::activeWindow() ? QApplication::activeWindow() : &window; }
const std::pair<PlayerActions::Id,double> rates[] = {{PlayerActions::RateHalf,.5},{PlayerActions::RateThreeQuarter,.75},{PlayerActions::RateReset,1},
    {PlayerActions::Rate125,1.25},{PlayerActions::Rate150,1.5},{PlayerActions::Rate200,2},{PlayerActions::Rate300,3}};
QString known(const QVariant &value) { return !value.isValid() || value.toString().isEmpty() ? QStringLiteral("未知") : value.toString(); }
}

MediaControls::MediaControls(Player &player, PlayerActions &actions, VideoWidget &video, VideoSurface &surface, ControlBar &bar, QMainWindow &window)
    : QObject(&window), player_(player), actions_(actions), video_(video), surface_(surface), bar_(bar), window_(window)
{
    setObjectName("mediaControls"); captures_ = new CaptureService(player,video,this);
    const auto action = [&actions](PlayerActions::Id id) { return actions.get(id); };
    const auto step = [this,action](PlayerActions::Id id, const QString &property, double delta) {
        connect(action(id), &QAction::triggered, this, [this,property,delta] { player_.adjustMediaProperty(property,delta); });
    };
    step(PlayerActions::AudioDelayLess,"audio-delay",-.1); step(PlayerActions::AudioDelayMore,"audio-delay",.1);
    step(PlayerActions::SubDelayLess,"sub-delay",-.1); step(PlayerActions::SubDelayMore,"sub-delay",.1);
    step(PlayerActions::SubScaleUp,"sub-scale",.1); step(PlayerActions::SubScaleDown,"sub-scale",-.1);
    step(PlayerActions::RateLess,"speed",-.25); step(PlayerActions::RateMore,"speed",.25);
    for (auto [id,rate] : rates) connect(action(id), &QAction::triggered, this, [this,rate] { player_.setMediaProperty("speed",rate); refresh(); });
    for (auto [id,property] : {std::pair{PlayerActions::AudioDelayReset,QString("audio-delay")},{PlayerActions::SubDelayReset,QString("sub-delay")},{PlayerActions::SubScaleReset,QString("sub-scale")}})
        connect(action(id), &QAction::triggered, this, [this,property] { player_.setMediaProperty(property,property == "sub-scale" ? 1.0 : 0.0); });
    connect(action(PlayerActions::AudioNext), &QAction::triggered, this, [this] { player_.nextTrack("audio"); });
    connect(action(PlayerActions::SubNext), &QAction::triggered, this, [this] { player_.nextTrack("sub"); });
    connect(action(PlayerActions::SubToggle), &QAction::triggered, this, [this] { player_.setMediaProperty("sub-visibility",!player_.propertyTarget("sub-visibility").toBool()); refresh(); });
    connect(action(PlayerActions::Pitch), &QAction::triggered, this, [this] { player_.setMediaProperty("audio-pitch-correction",!player_.propertyTarget("audio-pitch-correction").toBool()); refresh(); });
    connect(action(PlayerActions::SubLoad), &QAction::triggered, this, &MediaControls::loadSubtitle);
    connect(action(PlayerActions::FrameForward), &QAction::triggered, &player_, &Player::frameStep);
    connect(action(PlayerActions::LoopA), &QAction::triggered, &player_, &Player::setLoopA);
    connect(action(PlayerActions::LoopB), &QAction::triggered, &player_, &Player::setLoopB);
    connect(action(PlayerActions::LoopClear), &QAction::triggered, &player_, &Player::clearLoop);
    connect(action(PlayerActions::CaptureSource), &QAction::triggered, captures_, &CaptureService::source);
    connect(action(PlayerActions::CaptureViewport), &QAction::triggered, captures_, &CaptureService::viewport);
    connect(action(PlayerActions::CaptureFolder), &QAction::triggered, this, [this] { QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(captures_->lastSaved()).absolutePath())); });
    connect(action(PlayerActions::FileLocation), &QAction::triggered, this, [this] { QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(player_.path()).absolutePath())); });
    connect(action(PlayerActions::MediaInfo), &QAction::triggered, this, &MediaControls::showInformation);
    connect(action(PlayerActions::AlwaysOnTop), &QAction::triggered, this, &MediaControls::setAlwaysOnTop);
    connect(captures_, &CaptureService::saved, this, [this](const QString &path) { actions_.get(PlayerActions::CaptureFolder)->setEnabled(true); surface_.showCaptureSaved(path,actions_.get(PlayerActions::CaptureFolder)); });
    connect(captures_, &CaptureService::failed, this, [this](const QString &message) { surface_.showOsd("error",message,4000); });
    connect(&player_, &Player::notice, this, [this](const QString &message) { surface_.showOsd("media",message,2500); });
    const auto queueRefresh = [this] {
        if (refreshPending_) return; refreshPending_ = true;
        QTimer::singleShot(0,this,[this] { refreshPending_ = false; refresh(); });
    };
    connect(&player_, &Player::detailsChanged, this, queueRefresh);
    connect(&player_, &Player::mediaChanged, this, queueRefresh);
    connect(&player_, &Player::videoSizeChanged, this, queueRefresh);
    connect(&player_, &Player::videoTransformChanged, this, queueRefresh);
    connect(&player_, &Player::seekableChanged, this, queueRefresh);
    connect(&video_, &QOpenGLWidget::frameSwapped, this, [this] {
        if ((player_.videoTransformReady() && video_.canCaptureTransform(player_.videoTransformVersion())) != captureReady_) refresh();
    });
    auto *audio = window.findChild<QMenu *>("audioMenu");
    audioTracks_ = audio->addMenu(QStringLiteral("音轨")); audioTracks_->setObjectName("audioTracksMenu");
    audio->addAction(action(PlayerActions::AudioNext)); audio->addSeparator();
    audioDelay_ = numberControl(audio,"audio-delay","audioDelaySpin",-3600,3600,QStringLiteral("音频延迟（秒，正值表示音频较晚）"));
    for (auto id : {PlayerActions::AudioDelayLess,PlayerActions::AudioDelayMore,PlayerActions::AudioDelayReset}) audio->addAction(action(id));
    auto *sub = new QMenu(QStringLiteral("字幕(&S)"),&window); sub->setObjectName("subtitleMenu");
    window.findChild<QMenu *>("playerMenu")->insertMenu(window.findChild<QMenu *>("viewMenu")->menuAction(),sub);
    sub->addAction(action(PlayerActions::SubLoad)); sub->addAction(action(PlayerActions::SubToggle));
    subtitleTracks_ = sub->addMenu(QStringLiteral("字幕轨道")); subtitleTracks_->setObjectName("subtitleTracksMenu");
    sub->addAction(action(PlayerActions::SubNext)); sub->addSeparator();
    auto *note = new QWidgetAction(sub); auto *label = new QLabel(QStringLiteral("字幕跟随画面缩放，放大后可能裁切")); label->setMargin(8); label->setWordWrap(true); label->setMaximumWidth(320); note->setDefaultWidget(label); sub->addAction(note);
    sub->addAction(action(PlayerActions::ResetView));
    subDelay_ = numberControl(sub,"sub-delay","subDelaySpin",-3600,3600,QStringLiteral("字幕延迟（秒，正值表示字幕较晚）"));
    for (auto id : {PlayerActions::SubDelayLess,PlayerActions::SubDelayMore,PlayerActions::SubDelayReset}) sub->addAction(action(id));
    subScale_ = numberControl(sub,"sub-scale","subScaleSpin",.5,2,QStringLiteral("字幕字体缩放（ASS 保留自带样式）"));
    for (auto id : {PlayerActions::SubScaleUp,PlayerActions::SubScaleDown,PlayerActions::SubScaleReset}) sub->addAction(action(id));
    auto *play = window.findChild<QMenu *>("playMenu"); rateMenu_ = play->addMenu(QStringLiteral("倍速")); rateMenu_->setObjectName("rateMenu");
    for (auto [id,rate] : rates) rateMenu_->addAction(action(id));
    rateMenu_->addSeparator(); for (auto id : {PlayerActions::RateLess,PlayerActions::RateMore,PlayerActions::Pitch}) rateMenu_->addAction(action(id));
    play->addAction(action(PlayerActions::FrameForward));
    for (auto id : {PlayerActions::LoopA,PlayerActions::LoopB,PlayerActions::LoopClear}) play->addAction(action(id));
    auto *picture = window.findChild<QMenu *>("pictureMenu"); picture->addSeparator();
    picture->addAction(action(PlayerActions::CaptureSource)); picture->addAction(action(PlayerActions::CaptureViewport)); picture->addAction(action(PlayerActions::CaptureFolder));
    auto *view = window.findChild<QMenu *>("viewMenu"); view->addAction(action(PlayerActions::AlwaysOnTop)); view->addAction(action(PlayerActions::MediaInfo));
    window.findChild<QMenu *>("fileMenu")->addAction(action(PlayerActions::FileLocation));
    auto *context = window.findChild<QMenu *>("videoContextMenu");
    context->addMenu(rateMenu_); context->addMenu(audio); context->addMenu(sub);
    for (auto id : {PlayerActions::CaptureSource,PlayerActions::CaptureViewport,PlayerActions::MediaInfo}) context->addAction(action(id));
    refresh();
}

QDoubleSpinBox *MediaControls::numberControl(QMenu *menu, const QString &property, const QString &name, double minimum, double maximum, const QString &description)
{
    auto *action = new QWidgetAction(menu); auto *widget = new QWidget; auto *layout = new QVBoxLayout(widget);
    auto *title = new QLabel(description); title->setWordWrap(true); title->setMaximumWidth(300); layout->addWidget(title);
    auto *spin = new QDoubleSpinBox; spin->setObjectName(name); spin->setAccessibleName(description); spin->setRange(minimum,maximum);
    spin->setDecimals(property == "sub-scale" ? 1 : 3); spin->setSingleStep(.1); spin->setKeyboardTracking(false); layout->addWidget(spin);
    connect(spin,&QDoubleSpinBox::valueChanged,this,[this,property](double value) { player_.setMediaProperty(property,value); });
    action->setDefaultWidget(widget); menu->addAction(action); return spin;
}
void MediaControls::tracksMenu(QMenu *menu, const QString &type)
{
    if (menu->isVisible()) {
        for (auto *action : menu->actions()) if (action->property("trackId").isValid()) action->setChecked(action->property("trackId").toLongLong() == player_.mediaProperty(type == "audio" ? "aid" : "sid").toLongLong());
        return;
    }
    menu->clear(); const auto generation = player_.generation();
    for (const auto &track : player_.tracks()) if (track.type == type) {
        auto *action = menu->addAction(track.label()); action->setObjectName(QStringLiteral("%1.track.%2.%3").arg(type).arg(generation).arg(track.id));
        action->setProperty("trackId",track.id); action->setCheckable(true); action->setAutoRepeat(false);
        action->setChecked(track.id == player_.mediaProperty(type == "audio" ? "aid" : "sid").toLongLong());
        action->setToolTip(track.externalPath.isEmpty() ? track.label() : track.externalPath);
        action->setEnabled(player_.state() == Player::State::Loaded);
        connect(action,&QAction::triggered,this,[this,type,id=track.id,generation] { player_.selectTrack(type,id,generation); refresh(); });
    }
    if (menu->actions().isEmpty()) { auto *empty = menu->addAction(type == "audio" ? QStringLiteral("没有可用音轨") : QStringLiteral("没有可用字幕")); empty->setEnabled(false); }
}
void MediaControls::refresh()
{
    const bool loaded = player_.state() == Player::State::Loaded;
    int audio = 0, subs = 0; for (const auto &track : player_.tracks()) { audio += track.type == "audio"; subs += track.type == "sub"; }
    for (auto id : {PlayerActions::AudioDelayLess,PlayerActions::AudioDelayMore,PlayerActions::AudioDelayReset}) actions_.get(id)->setEnabled(loaded && audio > 0);
    actions_.get(PlayerActions::AudioNext)->setEnabled(loaded && audio > 1);
    actions_.get(PlayerActions::SubLoad)->setEnabled(loaded);
    actions_.get(PlayerActions::SubNext)->setEnabled(loaded && subs > 1);
    for (auto id : {PlayerActions::SubToggle,PlayerActions::SubDelayLess,PlayerActions::SubDelayMore,PlayerActions::SubDelayReset,PlayerActions::SubScaleUp,PlayerActions::SubScaleDown,PlayerActions::SubScaleReset}) actions_.get(id)->setEnabled(loaded && subs > 0);
    actions_.get(PlayerActions::SubToggle)->setChecked(player_.mediaProperty("sub-visibility").toBool());
    const double speed = player_.mediaProperty("speed").toDouble();
    for (auto [id,rate] : rates) { actions_.get(id)->setEnabled(loaded || player_.recovering()); actions_.get(id)->setChecked(std::abs(rate-speed) < .0001); }
    for (auto id : {PlayerActions::RateLess,PlayerActions::RateMore,PlayerActions::Pitch}) actions_.get(id)->setEnabled(loaded || player_.recovering());
    actions_.get(PlayerActions::Pitch)->setChecked(player_.mediaProperty("audio-pitch-correction").toBool());
    rateMenu_->setTitle(QStringLiteral("倍速 · %1×").arg(speed,0,'f',2));
    captureReady_ = player_.videoTransformReady() && video_.canCaptureTransform(player_.videoTransformVersion());
    for (auto id : {PlayerActions::CaptureSource,PlayerActions::CaptureViewport}) actions_.get(id)->setEnabled(loaded && captureReady_);
    actions_.get(PlayerActions::FrameForward)->setEnabled(loaded && video_.canCapture());
    for (auto id : {PlayerActions::LoopA,PlayerActions::LoopB,PlayerActions::LoopClear}) actions_.get(id)->setEnabled(loaded && player_.seekable());
    actions_.get(PlayerActions::CaptureFolder)->setEnabled(!captures_->lastSaved().isEmpty());
    actions_.get(PlayerActions::MediaInfo)->setEnabled(!player_.path().isEmpty()); actions_.get(PlayerActions::FileLocation)->setEnabled(QFileInfo::exists(player_.path()));
    actions_.get(PlayerActions::AlwaysOnTop)->setChecked(window_.windowFlags().testFlag(Qt::WindowStaysOnTopHint));
    for (auto [spin,name,enabled] : {std::tuple{audioDelay_,QString("audio-delay"),loaded && audio > 0}, {subDelay_,QString("sub-delay"),loaded && subs > 0}, {subScale_,QString("sub-scale"),loaded && subs > 0}}) {
        const QSignalBlocker blocker(spin); spin->setValue(player_.mediaProperty(name).toDouble()); spin->setEnabled(enabled);
    }
    bar_.setLoop(player_.mediaProperty("ab-loop-a"),player_.mediaProperty("ab-loop-b"));
    tracksMenu(audioTracks_,"audio"); tracksMenu(subtitleTracks_,"sub"); actions_.refreshIcons();
}
void MediaControls::loadSubtitle()
{
    const auto path = QFileDialog::getOpenFileName(owner(window_),QStringLiteral("加载外挂字幕"),{},QStringLiteral("字幕 (*.srt *.ass *.ssa *.vtt);;所有文件 (*)"));
    if (!path.isEmpty()) player_.addSubtitle(path);
}
void MediaControls::setAlwaysOnTop(bool enabled)
{
    actions_.get(PlayerActions::AlwaysOnTop)->setChecked(enabled);
    if (window_.windowFlags().testFlag(Qt::WindowStaysOnTopHint) == enabled) return;
    if (!window_.isVisible()) {
        window_.setWindowFlag(Qt::WindowStaysOnTopHint, enabled);
        emit alwaysOnTopChanged();
        return;
    }
    const auto state = window_.windowState(); const auto normal = window_.normalGeometry(); QPointer<QWidget> focus = QApplication::focusWidget();
    // On Windows changing the native flags exits fullscreen. Leave it explicitly
    // first so showFullScreen() performs a real transition after the flags change.
    if (state.testFlag(Qt::WindowFullScreen)) window_.setWindowState(state & ~Qt::WindowFullScreen);
    window_.setWindowFlag(Qt::WindowStaysOnTopHint, enabled);
    if (state.testFlag(Qt::WindowFullScreen)) window_.showFullScreen();
    else if (state.testFlag(Qt::WindowMaximized)) window_.showMaximized();
    else { window_.setGeometry(normal); window_.showNormal(); }
    if (focus) focus->setFocus(); refresh();
    emit alwaysOnTopChanged();
}
QVariantMap MediaControls::information() const
{
    const QFileInfo file(player_.path()); const auto video = player_.mediaProperty("video-params").toMap(), audio = player_.mediaProperty("audio-params").toMap();
    QVariantMap values{{QStringLiteral("文件"),player_.path()},{QStringLiteral("大小（字节）"),file.exists() ? QVariant(file.size()) : QVariant()},
        {QStringLiteral("容器"),player_.mediaProperty("file-format")},{QStringLiteral("视频编码/解码器"),player_.mediaProperty("video-codec")},
        {QStringLiteral("音频编码"),player_.mediaProperty("audio-codec-name")},{QStringLiteral("容器帧率"),player_.mediaProperty("container-fps")},
        {QStringLiteral("视频宽度"),video.value("w")},{QStringLiteral("视频高度"),video.value("h")},{QStringLiteral("像素格式"),video.value("pixelformat")},
        {QStringLiteral("色彩基色"),video.value("primaries")},{QStringLiteral("传递函数"),video.value("gamma")},{QStringLiteral("色彩矩阵"),video.value("colormatrix")},
        {QStringLiteral("采样率"),audio.value("samplerate")},{QStringLiteral("声道"),audio.value("hr-channels")},{QStringLiteral("倍速"),player_.mediaProperty("speed")},
        {QStringLiteral("音频延迟"),player_.mediaProperty("audio-delay")},{QStringLiteral("字幕延迟"),player_.mediaProperty("sub-delay")}};
    return values;
}
void MediaControls::showInformation()
{
    QDialog dialog(owner(window_)); dialog.setObjectName("mediaInfoDialog"); dialog.setWindowTitle(QStringLiteral("媒体信息"));
    auto *layout = new QVBoxLayout(&dialog); auto *table = new QTableWidget(0,2); table->setObjectName("mediaInfoTable");
    table->setHorizontalHeaderLabels({QStringLiteral("属性"),QStringLiteral("实际值")}); table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setAccessibleName(QStringLiteral("当前媒体实际信息")); table->verticalHeader()->hide(); table->horizontalHeader()->setStretchLastSection(true); layout->addWidget(table);
    const auto update = [this,table] { const auto values = information(); table->setRowCount(values.size()); int row = 0;
        for (auto it = values.begin(); it != values.end(); ++it,++row) { table->setItem(row,0,new QTableWidgetItem(it.key())); table->setItem(row,1,new QTableWidgetItem(known(it.value()))); } table->resizeColumnToContents(0); };
    connect(&player_,&Player::detailsChanged,&dialog,update); update();
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close); layout->addWidget(buttons); connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    dialog.resize(720,560); dialog.exec();
}
