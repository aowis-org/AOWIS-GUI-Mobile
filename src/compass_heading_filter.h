#pragma once

#include "compass_heading.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

// A circular, noise-gated heading observer. Compass azimuth is an absolute
// measurement, not an angular-rate sensor: differentiating individual magnetic
// readings injects magnetic noise directly into the animation velocity.
//
// We estimate turning speed from a short least-squares history, smooth that
// speed, and use a hysteretic still/moving state. The still state holds the
// needle *exactly* still instead of rendering subpixel noise. During motion a
// continuously updated, bounded velocity prediction fills sensor gaps.
// A frame-time-invariant first-order display follower avoids spring ringing.
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
        this->m_moving = false;
        this->m_lastSampleSeconds = 0.0;
        this->m_lastFrameSeconds = -1.0;
        this->m_rawDegrees = 0.0;
        this->m_fastDegrees = 0.0;
        this->m_slowDegrees = 0.0;
        this->m_estimateDegrees = 0.0;
        this->m_velocity = 0.0;
        this->m_displayDegrees = 0.0;
        this->m_displayTarget = 0.0;
        this->m_quietSeconds = 0.0;
        this->m_motionConfirmations = 0;
        this->m_historySize = 0;
    }

    bool initialized() const { return this->m_initialized; }
    bool moving() const { return this->m_moving; }
    double targetDegrees() const { return normalize(this->m_estimateDegrees); }
    double displayDegrees() const { return normalize(this->m_displayDegrees); }
    double continuousDisplayDegrees() const { return this->m_displayDegrees; }
    double velocityDegreesPerSecond() const { return this->m_velocity; }

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
            this->m_rawDegrees = measured;
            this->m_fastDegrees = measured;
            this->m_slowDegrees = measured;
            this->m_estimateDegrees = measured;
            this->m_displayDegrees = measured;
            this->m_displayTarget = measured;
            this->appendReading(monotonicSeconds, measured);
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

        // Unwrap each measurement relative to the preceding raw measurement,
        // not the displayed heading. This also handles repeated north crossings.
        const double unwrapped = this->m_rawDegrees + shortestDelta(this->m_rawDegrees, measured);
        this->m_rawDegrees = unwrapped;
        // Two time scales distinguish persistent physical turns from magnetic
        // shimmer: do not use an instantaneous angular derivative as a motion
        // detector (that amplifies high-frequency sensor noise).
        this->m_fastDegrees += (1.0 - std::exp(-dt / 0.09)) *
                               (unwrapped - this->m_fastDegrees);
        this->m_slowDegrees += (1.0 - std::exp(-dt / 0.38)) *
                               (unwrapped - this->m_slowDegrees);
        this->appendReading(monotonicSeconds, unwrapped);
        const double observedVelocity = this->regressionVelocity();
        const double velocityAlpha = 1.0 - std::exp(-dt / 0.18);
        this->m_velocity += velocityAlpha * (observedVelocity - this->m_velocity);
        this->m_velocity = std::clamp(this->m_velocity, -540.0, 540.0);

        if (!this->m_moving)
        {
            const double departure = std::fabs(unwrapped - this->m_estimateDegrees);
            // A turn changes the fast average before the slow average.
            // Very slow turns eventually drift the slow baseline away from
            // the held instrument heading. Both require confirmation.
            const bool realTurn =
                (departure > 0.65 &&
                 std::fabs(this->m_fastDegrees - this->m_slowDegrees) > 2.0) ||
                std::fabs(this->m_slowDegrees - this->m_estimateDegrees) > 2.6;
            this->m_motionConfirmations = realTurn ? this->m_motionConfirmations + 1 : 0;
            if (this->m_motionConfirmations >= 2)
            {
                this->m_moving = true;
                this->m_quietSeconds = 0.0;
            }
            else
            {
                // Absolute magnetic noise below the motion threshold must
                // not produce visible oscillation or angular-rate prediction.
                this->m_displayTarget = this->m_estimateDegrees;
                this->m_lastSampleSeconds = monotonicSeconds;
                return true;
            }
        }

        // Correct a velocity-predicted absolute heading toward the magnetic
        // measurement. Low pass the position correction; the velocity itself
        // comes from a multi-sample fit, never a single noisy innovation.
        const double predicted = this->m_estimateDegrees + this->m_velocity * dt;
        const double alpha = 1.0 - std::exp(-dt / 0.10);
        this->m_estimateDegrees = predicted + alpha * (unwrapped - predicted);

        // Hold the instrument still after physical turning stops. Check
        // the *filtered* motion indicator, not magnetic sample residuals.
        const bool quiet = std::fabs(this->m_fastDegrees - this->m_slowDegrees) < 1.0 &&
                           std::fabs(this->m_velocity) < 12.0;
        if (quiet)
            this->m_quietSeconds += dt;
        else
            this->m_quietSeconds = std::max(0.0, this->m_quietSeconds - dt);
        if (this->m_quietSeconds >= 0.30)
        {
            this->m_moving = false;
            this->m_motionConfirmations = 0;
            this->m_velocity = 0.0;
        }

        this->m_lastSampleSeconds = monotonicSeconds;
        return true;
    }

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
        const double horizon = this->m_moving ? std::min(age, 0.09) : 0.0;
        this->m_displayTarget = this->m_estimateDegrees + this->m_velocity * horizon;
        // A first-order low pass cannot ring or change direction by itself.
        // 120ms time constant; independent of actual rendering refresh rate.
        const double gain = 1.0 - std::exp(-dt / 0.12);
        const double next = this->m_displayDegrees +
                            gain * (this->m_displayTarget - this->m_displayDegrees);
        if (!this->m_moving && std::fabs(next - this->m_displayDegrees) < 0.025 &&
            std::fabs(this->m_displayTarget - this->m_displayDegrees) < 0.20)
            return false;
        const double previous = this->m_displayDegrees;
        this->m_displayDegrees = next;
        return std::fabs(this->m_displayDegrees - previous) >= 0.002;
    }

private:
    struct Observation
    {
        double seconds = 0.0;
        double heading = 0.0;
    };

    void appendReading(double seconds, double heading)
    {
        // Keep a bounded ~300ms window. Fit a line to the whole window,
        // not finite differences between noisy adjacent magnetic packets.
        std::size_t kept = 0;
        for (std::size_t index = 0; index < this->m_historySize; ++index)
        {
            if (seconds - this->m_history[index].seconds <= 0.50)
                this->m_history[kept++] = this->m_history[index];
        }
        this->m_historySize = kept;
        if (this->m_historySize == this->m_history.size())
        {
            for (std::size_t index = 1; index < this->m_historySize; ++index)
                this->m_history[index - 1] = this->m_history[index];
            --this->m_historySize;
        }
        this->m_history[this->m_historySize++] = {seconds, heading};
    }

    double regressionVelocity() const
    {
        if (this->m_historySize < 3)
            return 0.0;
        double sumT = 0.0;
        double sumH = 0.0;
        for (std::size_t i = 0; i < this->m_historySize; ++i)
        {
            sumT += this->m_history[i].seconds;
            sumH += this->m_history[i].heading;
        }
        const double meanT = sumT / static_cast<double>(this->m_historySize);
        const double meanH = sumH / static_cast<double>(this->m_historySize);
        double variance = 0.0;
        double covariance = 0.0;
        for (std::size_t i = 0; i < this->m_historySize; ++i)
        {
            const double t = this->m_history[i].seconds - meanT;
            variance += t * t;
            covariance += t * (this->m_history[i].heading - meanH);
        }
        return variance > 1e-7 ? std::clamp(covariance / variance, -540.0, 540.0) : 0.0;
    }

    bool m_initialized = false;
    bool m_moving = false;
    double m_lastSampleSeconds = 0.0;
    double m_lastFrameSeconds = -1.0;
    double m_rawDegrees = 0.0;
    double m_fastDegrees = 0.0;
    double m_slowDegrees = 0.0;
    double m_estimateDegrees = 0.0; // unwrapped
    double m_velocity = 0.0;
    double m_displayDegrees = 0.0; // unwrapped
    double m_displayTarget = 0.0;
    double m_quietSeconds = 0.0;
    int m_motionConfirmations = 0;
    std::array<Observation, 32> m_history{};
    std::size_t m_historySize = 0;
};
}
