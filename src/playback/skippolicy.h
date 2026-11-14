#pragma once

#include <cmath>

struct SkipPolicy {
    enum class Bounds { Waiting, Ready, Invalid };

    int introSeconds = 0, outroSeconds = 0;
    double outroStart = 0;
    bool watchingOutro = false;
    bool allowOutro = true;

    bool enabled() const { return introSeconds > 0 || outroSeconds > 0; }

    Bounds updateBounds(double duration, bool seekable, double position)
    {
        if (!enabled()) return Bounds::Ready;
        if (!seekable || !std::isfinite(duration) || duration <= 0) {
            outroStart = 0;
            return Bounds::Waiting;
        }
        if (duration - introSeconds - outroSeconds < 1) {
            *this = {};
            return Bounds::Invalid;
        }
        const double boundary = duration - outroSeconds;
        if (outroSeconds && boundary != outroStart && position >= boundary) allowOutro = false;
        outroStart = outroSeconds && allowOutro ? boundary : 0;
        return Bounds::Ready;
    }

    bool inOutro(double position) const { return outroStart > 0 && position >= outroStart; }
    bool crossedOutro(double before, double now) const
    {
        return !watchingOutro && !inOutro(before) && inOutro(now);
    }
};
