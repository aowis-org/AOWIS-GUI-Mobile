#pragma once

#include <algorithm>
#include <cmath>

// A physical plumb line, not a horizontal spirit level. Input accelerations
// are in Qt Sensors' fixed device axes (x right, y toward the phone top,
// z normal to its face). Gravity projected onto the screen defines 'down'.
// Screen orientation is deliberately a display transform, not a hardware
// orientation change or Android system auto-rotation request.
namespace AowisPlumbBob
{
constexpr double kPi = 3.14159265358979323846;
constexpr double kDegrees = 180.0 / kPi;

struct Reading
{
    double downX = 0.0;  // projected gravity, screen right positive
    double downY = 1.0;  // projected gravity, screen down positive
    bool suitable = false;
};

struct ScreenDirection
{
    double x = 0.0;
    double y = 1.0;
};

inline bool fromGravity(double x, double y, double z, Reading &out)
{
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
        return false;
    const double magnitude = std::hypot(std::hypot(x, y), z);
    if (magnitude < 1.0 || magnitude > 30.0)
        return false;
    const double planar = std::hypot(x, y);
    // A phone lying almost flat has no meaningful in-plane plumb direction.
    // The threshold is deliberately far from true vertical so the instrument
    // remains useful when measuring moderately leaning surfaces.
    const bool suitable = planar / magnitude >= 0.35;
    Reading value;
    value.suitable = suitable;
    if (suitable)
    {
        // Qt fixed axes: +x right, +y toward the device's top. The static
        // accelerometer measures opposing support acceleration, hence -x,
        // while the screen's downward axis is opposite Qt's +y.
        value.downX = -x / planar;
        value.downY = y / planar;
    }
    out = value;
    return true;
}

inline ScreenDirection screenDirection(ScreenDirection native, int clockwiseDegrees)
{
    switch (((clockwiseDegrees % 360) + 360) % 360)
    {
    case 90: return {native.y, -native.x};
    case 180: return {-native.x, -native.y};
    case 270: return {-native.y, native.x};
    default: return native;
    }
}

inline double edgeDeviation(ScreenDirection down)
{
    // Nearest vertical screen edge: upside-down is still plumb.
    return std::atan2(std::abs(down.x), std::abs(down.y)) * kDegrees;
}

inline bool plumb(double edgeDegrees, double toleranceDegrees = 0.75)
{
    return std::isfinite(edgeDegrees) && edgeDegrees >= 0.0 &&
           edgeDegrees <= toleranceDegrees;
}

inline double shortestDelta(double fromDegrees, double toDegrees)
{
    double delta = std::remainder(toDegrees - fromDegrees, 360.0);
    if (delta == -180.0)
        delta = 180.0;
    return delta;
}

// Two low-pass stages with real measured dt. Crucially, NO angular-velocity
// extrapolation: accelerometer noise must never vibrate a supposedly hanging
// physical weight. The display angle is continuous across the north crossing.
class Filter
{
public:
    void reset()
    {
        this->m_initialized = false;
        this->m_lastSample = -1.0;
        this->m_lastFrame = -1.0;
        this->m_observedAngle = 0.0;
        this->m_displayAngle = 0.0;
    }

    bool sample(Reading reading, double seconds)
    {
        if (!reading.suitable || !std::isfinite(seconds))
            return false;
        const double angle = std::atan2(reading.downX, reading.downY) * kDegrees;
        if (!this->m_initialized)
        {
            this->m_initialized = true;
            this->m_lastSample = seconds;
            this->m_lastFrame = seconds;
            this->m_observedAngle = angle;
            this->m_displayAngle = angle;
            return true;
        }
        if (seconds <= this->m_lastSample)
            return false;
        const double dt = std::clamp(seconds - this->m_lastSample, 0.0, 0.5);
        this->m_lastSample = seconds;
        const double difference = shortestDelta(this->m_observedAngle, angle);
        // Reject sub-0.12 degree variations without suppressing slow, genuine
        // turns: the accumulated difference is measured against the last
        // accepted filtered angle rather than the previous raw sample.
        if (std::abs(difference) > 0.12)
        {
            const double gain = 1.0 - std::exp(-dt / 0.13);
            this->m_observedAngle += gain * difference;
        }
        return true;
    }

    bool advance(double seconds)
    {
        if (!this->m_initialized || !std::isfinite(seconds) || seconds <= this->m_lastFrame)
            return false;
        const double dt = std::clamp(seconds - this->m_lastFrame, 0.0, 0.1);
        this->m_lastFrame = seconds;
        const double gain = 1.0 - std::exp(-dt / 0.10);
        const double beforeAngle = this->m_displayAngle;
        this->m_displayAngle += gain * (this->m_observedAngle - this->m_displayAngle);
        return std::abs(this->m_displayAngle - beforeAngle) > 0.0001;
    }

    ScreenDirection direction() const
    {
        const double angle = this->m_displayAngle / kDegrees;
        return {std::sin(angle), std::cos(angle)};
    }
    bool initialized() const { return this->m_initialized; }

private:
    bool m_initialized = false;
    double m_lastSample = -1.0;
    double m_lastFrame = -1.0;
    double m_observedAngle = 0.0;
    double m_displayAngle = 0.0;
};
} // namespace AowisPlumbBob
