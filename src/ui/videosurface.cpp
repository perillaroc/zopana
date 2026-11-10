#include "videosurface.h"
#include "videowidget.h"

#include <QAction>
#include <QHBoxLayout>
#include <QLabel>
#include <QResizeEvent>
#include <QToolButton>
#include <QVBoxLayout>

VideoSurface::VideoSurface(VideoWidget *video, QWidget *parent) : QWidget(parent), video_(video)
{
    setObjectName("videoSurface");
    video_->setParent(this);
    card_ = new QWidget(this); card_->setObjectName("stateCard");
    auto *layout = new QVBoxLayout(card_); layout->setContentsMargins(24, 20, 24, 20); layout->setSpacing(14);
    title_ = new QLabel; title_->setObjectName("cardTitle"); title_->setAlignment(Qt::AlignCenter);
    title_->setTextFormat(Qt::PlainText); title_->setWordWrap(true);
    body_ = new QLabel; body_->setObjectName("cardBody"); body_->setAlignment(Qt::AlignCenter);
    body_->setTextFormat(Qt::PlainText); body_->setWordWrap(true);
    layout->addWidget(title_); layout->addWidget(body_);
    auto *row = new QHBoxLayout; row->addStretch();
    primary_ = new QToolButton; primary_->setObjectName("cardPrimary");
    secondary_ = new QToolButton; secondary_->setObjectName("cardSecondary");
    for (auto *button : {primary_, secondary_}) {
        button->setToolButtonStyle(Qt::ToolButtonTextOnly); button->setMinimumHeight(36);
        button->setFocusPolicy(Qt::StrongFocus); row->addWidget(button);
    }
    row->addStretch(); layout->addLayout(row);
    osd_ = new QLabel(this); osd_->setObjectName("playerOsd"); osd_->setTextFormat(Qt::PlainText);
    osd_->setAttribute(Qt::WA_TransparentForMouseEvents); osd_->setWordWrap(true);
    osd_->setAutoFillBackground(true); osd_->setMargin(10); osd_->hide();
    osd_->setAccessibleName(QStringLiteral("播放操作提示"));
    osdTimer_.setSingleShot(true);
    connect(&osdTimer_, &QTimer::timeout, osd_, &QWidget::hide);
    captureFolder_ = new QToolButton(this); captureFolder_->setObjectName("captureFolderButton");
    captureFolder_->setMinimumSize(100,32); captureFolder_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon); captureFolder_->hide();
    connect(&osdTimer_, &QTimer::timeout, captureFolder_, &QWidget::hide);
}

void VideoSurface::setControls(QWidget *controls) { controls_ = controls; arrange(); }
void VideoSurface::setFullscreen(bool fullscreen) { fullscreen_ = fullscreen; arrange(); }
void VideoSurface::showOsd(const QString &kind, const QString &text, int milliseconds)
{
    captureFolder_->hide();
    osd_->setProperty("kind", kind); osd_->setText(text); osd_->setAccessibleDescription(text);
    osd_->setFixedWidth(qMin(480, qMax(180, width() - 48))); osd_->adjustSize();
    osd_->move(24, 24); osd_->show(); osd_->raise(); osdTimer_.start(milliseconds);
}
bool VideoSurface::dismissOsd()
{
    if (!osd_->isVisible()) return false;
    osdTimer_.stop(); osd_->hide(); captureFolder_->hide(); return true;
}
void VideoSurface::showCaptureSaved(const QString &path, QAction *openFolder)
{
    showOsd("capture",QStringLiteral("已保存：%1").arg(path),4000);
    captureFolder_->setDefaultAction(openFolder); captureFolder_->setAccessibleName(openFolder->text());
    captureFolder_->adjustSize(); captureFolder_->move(24,osd_->geometry().bottom()+6); captureFolder_->show(); captureFolder_->raise();
}

void VideoSurface::present(const QString &title, const QString &body, QAction *primary, QAction *secondary)
{
    title_->setText(title); body_->setText(body);
    for (auto [button, action] : {std::pair{primary_, primary}, {secondary_, secondary}}) {
        if (action) { button->setDefaultAction(action); button->setAccessibleName(action->text()); }
        button->setVisible(action != nullptr);
    }
    card_->show(); card_->raise(); arrange();
}

void VideoSurface::showVideo() { card_->hide(); }

void VideoSurface::arrange()
{
    QRect viewport = rect();
    if (controls_) {
        const int height = qMax(92, controls_->sizeHint().height());
        controls_->setGeometry(0, this->height() - height, width(), height);
        if (!fullscreen_) viewport.setBottom(controls_->y() - 1);
        controls_->raise();
    }
    video_->setGeometry(viewport);
    const int width = qMin(520, qMax(200, this->width() - 48));
    card_->setFixedWidth(width);
    card_->adjustSize();
    card_->move((this->width() - card_->width()) / 2, (viewport.height() - card_->height()) / 2);
}

void VideoSurface::resizeEvent(QResizeEvent *event) { QWidget::resizeEvent(event); arrange(); }
