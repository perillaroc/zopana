#pragma once

#include <QPointF>
#include <QSizeF>

#include <algorithm>

// Coordinates are logical pixels within the video widget, including letterboxing.
class ViewGeometry
{
public:
    bool valid() const { return !viewport_.isEmpty() && !display_.isEmpty(); }
    double scale() const { return scale_; }
    QPointF focus() const { return focus_; }
    QPointF topLeft() const { return topLeft_; }

    QSizeF frameSize() const
    {
        if (!valid())
            return {0, 0};
        const double fit = std::min(viewport_.width() / display_.width(),
                                    viewport_.height() / display_.height());
        return display_ * (fit * scale_);
    }

    void setSizes(QSizeF viewport, QSizeF display)
    {
        viewport_ = viewport;
        display_ = display;
        placeAtFocus();
    }

    void setFocus(double scale, QPointF focus)
    {
        scale_ = std::clamp(scale, 1.0, 8.0);
        focus_ = focus;
        placeAtFocus();
    }

    void reset()
    {
        setFocus(1, {0.5, 0.5});
    }

    void zoomAt(QPointF pointer, double newScale)
    {
        if (!valid())
            return;
        const auto frame = frameSize();
        const QPointF anchor(std::clamp(pointer.x(), std::max(0.0, topLeft_.x()),
                                        std::min(viewport_.width(), topLeft_.x() + frame.width())),
                             std::clamp(pointer.y(), std::max(0.0, topLeft_.y()),
                                        std::min(viewport_.height(), topLeft_.y() + frame.height())));
        const double previous = scale_;
        scale_ = std::clamp(newScale, 1.0, 8.0);
        moveTo(anchor + (scale_ / previous) * (topLeft_ - anchor));
    }

    void moveTo(QPointF position)
    {
        if (!valid())
            return;
        placeFrame(position);
        const auto frame = frameSize();
        focus_ = {(viewport_.width() / 2 - topLeft_.x()) / frame.width(),
                  (viewport_.height() / 2 - topLeft_.y()) / frame.height()};
    }

private:
    void placeFrame(QPointF position)
    {
        const auto frame = frameSize();
        auto limit = [](double origin, double viewport, double length) {
            return length > viewport ? std::clamp(origin, viewport - length, 0.0)
                                     : (viewport - length) / 2;
        };
        topLeft_ = {limit(position.x(), viewport_.width(), frame.width()),
                    limit(position.y(), viewport_.height(), frame.height())};
    }

    void placeAtFocus()
    {
        // Layout can temporarily center or constrain an axis. Keep the
        // chosen content point for a later viewport or video that can show it.
        if (!valid()) {
            topLeft_ = {};
            return;
        }
        const auto frame = frameSize();
        placeFrame({viewport_.width() / 2 - focus_.x() * frame.width(),
                    viewport_.height() / 2 - focus_.y() * frame.height()});
    }

    QSizeF viewport_;
    QSizeF display_;
    double scale_ = 1;
    QPointF focus_{0.5, 0.5};
    QPointF topLeft_;
};
