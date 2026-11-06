#pragma once

#include "compass_heading.h"

#include <algorithm>
#include <cmath>

// Circular constant-velocity observer with a critically damped display servo.
//
// The observer extracts angular motion from irregular magnetic samples and
// suppresses stationary jitter. Between samples, the display follows a bounded
// velocity prediction rather than repeatedly slowing to a stop at each sample.
// A persistent Qt animation driver (not a QTimer or restarted QML tween) calls
// advanceDisplay() at the scene graph's animation cadence.
namespace AowisCompass
{
inline double shortestDelta(double fromDegrees, double toDegrees)
{
    return std::remainder(toDegrees - fromDegrees, 360.0);
}

class HeadingFilter final
{
public:
    void reset()
    {
        this->m_initialized = false;
        this->m_lastSampleSeconds = 0.0;
        this->m_lastFrameSeconds = -1.0;
        this->m_estimateDegrees = 0.0;
        this->m_estimatedVelocity = 0.0;
        this->m_displayDegrees = 0.0;
        this->m_displayVelocity = 0.0;
    }

    bool initialized() const { return this->m_initialized; }
    double targetDegrees() const { return normalize(this->m_estimateDegrees); }
    double displayDegrees() const { return normalize(this->m_displayDegrees); }
    double velocityDegreesPerSecond() const { return this->m_estimatedVelocity; }

    bool sample(double rawDegrees, double monotonicSeconds)
    {
        const double measured = normalize(rawDegrees);
        if (measured < 0.0 || !std::isfinite(monotonicSeconds))
            return false;
        if (!this->m_initialized)
        {
            this->m_initialized = true;
            this->m_lastSampleSeconds = monotonicSeconds;
            this->m_lastFrameSeconds = monotonicSeconds;
            this->m_estimateDegrees = measured;
            this->m_displayDegrees = measured;
            return true;
        }

        const double dt = monotonicSeconds - this->m_lastSampleSeconds;
        if (dt <= 0.0)
            return false;
        if (dt > 1.0)
        {
            this->reset();
            return this->sample(measured, monotonicSeconds);
        }

        const double predicted = this->m_estimateDegrees + this->m_estimatedVelocity * dt;
        const double residual = shortestDelta(predicted, measured);
        // Adaptive position correction: small disturbances receive more
        // smoothing, genuine changes are followed promptly. Never interpret
        // a wrap at north as a full revolution.
        const double gain = 1.0 - std::exp(-dt / (std::fabs(residual) > 2.0 ? 0.065 : 0.145));
        this->m_estimateDegrees = predicted + gain * residual;

        // Alpha-beta observer: update angular velocity from innovations,
        // damping low-rate magnetic drift when the phone is still.
        const double velocityGain = std::fabs(residual) > 2.0 ? 0.20 : 0.085;
        this->m_estimatedVelocity += (velocityGain / dt) * residual;
        if (std::fabs(residual) < 0.8 && std::fabs(this->m_estimatedVelocity) < 16.0)
            this->m_estimatedVelocity *= std::exp(-dt / 0.18);
        this->m_estimatedVelocity = std::clamp(this->m_estimatedVelocity, -540.0, 540.0);
        this->m_lastSampleSeconds = monotonicSeconds;
        return true;
    }

    // Evaluate every rendered animation frame, not every magnetic packet.
    // A short velocity look-ahead closes the gaps between sensor readings;
    // the prediction is bounded so missing packets cannot cause endless spin.
    bool advanceDisplay(double monotonicSeconds)
    {
        if (!this->m_initialized || !std::isfinite(monotonicSeconds) ||
            monotonicSeconds < this->m_lastFrameSeconds)
            return false;

        const double dt = std::min(monotonicSeconds - this->m_lastFrameSeconds, 0.08);
        this->m_lastFrameSeconds = monotonicSeconds;
        if (dt <= 0.0)
            return false;

        const double age = std::max(0.0, monotonicSeconds - this->m_lastSampleSeconds);
        const double horizon = std::min(age, 0.105);
        const double target = this->m_estimateDegrees + this->m_estimatedVelocity * horizon;
        const double targetVelocity = age < 0.105 ? this->m_estimatedVelocity : 0.0;
        const double startHorizon = std::min(std::max(0.0, age - dt), 0.105);
        const double targetAtStart = this->m_estimateDegrees +
                                     this->m_estimatedVelocity * startHorizon;

        // Critically damped second-order follower, integrated analytically.
        // No spring overshoot, discontinuous easing restart or frame-rate
        // dependent approximation; retains display velocity across samples.
        constexpr double omega = 21.0; // rad/s (~140 ms visible settling)
        const double error = this->m_displayDegrees - targetAtStart;
        const double relativeVelocity = this->m_displayVelocity - targetVelocity;
        const double rate = relativeVelocity + omega * error;
        const double decay = std::exp(-omega * dt);
        const double nextError = (error + rate * dt) * decay;
        const double nextRelativeVelocity = (relativeVelocity - omega * rate * dt) * decay;
        const double previous = this->m_displayDegrees;
        this->m_displayDegrees = target + nextError;
        this->m_displayVelocity = targetVelocity + nextRelativeVelocity;
        return std::fabs(this->m_displayDegrees - previous) >= 0.002;
    }

private:
    bool m_initialized = false;
    double m_lastSampleSeconds = 0.0;
    double m_lastFrameSeconds = -1.0;
    double m_estimateDegrees = 0.0; // unwrapped angle, permits continuous turns
    double m_estimatedVelocity = 0.0;
    double m_displayDegrees = 0.0; // unwrapped, continuous through north
    double m_displayVelocity = 0.0;
};
}
