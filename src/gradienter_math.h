#pragma once

#include <algorithm>
#include <cmath>

// Hardware-independent two-axis gravity level. The axes refer to the phone's
// native screen plane; screen rotation is applied separately. No magnetic
// heading, location or map renderer dependency.
namespace AowisGradienter
{
struct Angles
{
    double x = 0.0; // right-edge tilt in degrees
    double y = 0.0; // top-edge tilt in degrees
};

inline bool finite(Angles value)
{
    return std::isfinite(value.x) && std::isfinite(value.y);
}

inline bool fromGravity(double x, double y, double z, Angles &result)
{
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
        return false;
    const double magnitude = std::hypot(std::hypot(x, y), z);
    // Reject unusable readings and violent accelerations. Qt Gravity mode
    // returns a gravity-only estimate where the backend supports it.
    if (magnitude < 1.0 || magnitude > 30.0)
        return false;
    constexpr double degrees = 180.0 / 3.14159265358979323846;
    result.x = degrees * std::atan2(x, std::hypot(y, z));
    result.y = degrees * std::atan2(y, std::hypot(x, z));
    return finite(result);
}

inline Angles screenAxes(Angles axes, int clockwiseDegrees)
{
    const int angle = ((clockwiseDegrees % 360) + 360) % 360;
    switch (angle)
    {
    case 90: return Angles{axes.y, -axes.x};
    case 180: return Angles{-axes.x, -axes.y};
    case 270: return Angles{-axes.y, axes.x};
    default: return axes;
    }
}

inline double totalTilt(Angles axes)
{
    return std::hypot(axes.x, axes.y);
}

// Exact time-based exponential filtering: the smoothing is independent of
// backend sample frequency and Qt's display frame rate. No spring or velocity
// extrapolation: an inclinometer should settle rather than ring or predict.
class Filter
{
public:
    void reset()
    {
        this->m_hasSample = false;
        this->m_lastSampleSeconds = -1.0;
        this->m_lastFrameSeconds = -1.0;
        this->m_observed = Angles{};
        this->m_display = Angles{};
    }

    bool hasSample() const { return this->m_hasSample; }
    Angles displayed() const { return this->m_display; }

    bool sample(Angles value, double seconds)
    {
        if (!finite(value) || !std::isfinite(seconds))
            return false;
        if (!this->m_hasSample)
        {
            this->m_hasSample = true;
            this->m_lastSampleSeconds = seconds;
            this->m_lastFrameSeconds = seconds;
            this->m_observed = value;
            this->m_display = value;
            return true;
        }
        if (seconds <= this->m_lastSampleSeconds)
            return false;
        const double dt = std::clamp(seconds - this->m_lastSampleSeconds, 0.0, 0.5);
        this->m_lastSampleSeconds = seconds;
        const double gain = 1.0 - std::exp(-dt / 0.20);
        this->m_observed.x += gain * (value.x - this->m_observed.x);
        this->m_observed.y += gain * (value.y - this->m_observed.y);
        return true;
    }

    bool advance(double seconds)
    {
        if (!this->m_hasSample || !std::isfinite(seconds) || seconds <= this->m_lastFrameSeconds)
            return false;
        const double dt = std::clamp(seconds - this->m_lastFrameSeconds, 0.0, 0.10);
        this->m_lastFrameSeconds = seconds;
        const double gain = 1.0 - std::exp(-dt / 0.12);
        const Angles before = this->m_display;
        this->m_display.x += gain * (this->m_observed.x - this->m_display.x);
        this->m_display.y += gain * (this->m_observed.y - this->m_display.y);
        return std::abs(this->m_display.x - before.x) >= 0.0001 ||
               std::abs(this->m_display.y - before.y) >= 0.0001;
    }

private:
    bool m_hasSample = false;
    double m_lastSampleSeconds = -1.0;
    double m_lastFrameSeconds = -1.0;
    Angles m_observed;
    Angles m_display;
};
} // namespace AowisGradienter
