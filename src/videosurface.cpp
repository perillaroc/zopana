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
    video_->setGeometry(rect());
    const int width = qMin(520, qMax(200, this->width() - 48));
    card_->setFixedWidth(width);
    card_->adjustSize();
    card_->move((this->width() - card_->width()) / 2, (height() - card_->height()) / 2);
}

void VideoSurface::resizeEvent(QResizeEvent *event) { QWidget::resizeEvent(event); arrange(); }
