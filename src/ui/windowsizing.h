#pragma once

#include <QRect>
#include <QMargins>
#include <QtMath>

enum class WindowSizePolicy { LastGeometry, VideoScale, FitWorkArea };

struct VideoWindowSize {
    QRect client;
    bool docked;
};

inline VideoWindowSize solveVideoWindow(QSize video, double dpr, double scale, QRect workArea, QRect reference,
                                      QMargins frame, QSize minimum, int chromeHeight, int dockWidth, int dockThreshold)
{
    const QSize outerChrome(frame.left() + frame.right(), frame.top() + frame.bottom());
    const QSize available = (workArea.size() - outerChrome).expandedTo(QSize(1, 1));
    const int videoHeight = qMax(1, available.height() - chromeHeight);
    const double requested = scale > 0 ? scale / dpr : qInf();
    const auto fittingScale = [&](int sideWidth) {
        return qMin(double(qMax(1, available.width() - sideWidth)) / video.width(), double(videoHeight) / video.height());
    };
    const auto widthAt = [&](int sideWidth) { return qRound(video.width() * qMin(requested, fittingScale(sideWidth))); };
    const bool docked = dockWidth > 0 && available.width() >= dockThreshold
        && widthAt(dockWidth) + dockWidth >= dockThreshold;
    const int side = docked ? dockWidth : 0;
    const double actual = qMin(requested, fittingScale(side));
    const QSize desired(qRound(video.width() * actual) + side, qRound(video.height() * actual) + chromeHeight);
    const QSize clientSize = desired.expandedTo(minimum).boundedTo(available).expandedTo(QSize(1, 1));
    const QSize frameSize = clientSize + outerChrome;
    const QPoint ideal = reference.center() - QPoint(frameSize.width() / 2, frameSize.height() / 2);
    const QPoint outer(qBound(workArea.left(), ideal.x(), qMax(workArea.left(), workArea.right() - frameSize.width() + 1)),
                       qBound(workArea.top(), ideal.y(), qMax(workArea.top(), workArea.bottom() - frameSize.height() + 1)));
    return {QRect(outer + QPoint(frame.left(), frame.top()), clientSize), docked};
}
