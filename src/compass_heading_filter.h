#pragma once

#include "compass_heading.h"

#include <algorithm>
#include <cmath>

// Circular version of the One Euro speed-adaptive low-pass filter:
// Casiez, Roussel & Vogel, CHI 2012, DOI 10.1145/2207676.2208639.
//
// Samples are filtered at their *actual* arrival times. The QML-facing heading
// advances on an independent display timer, so 20 Hz sensor readings cannot
// produce visible 20 Hz rotation steps. All state is in C++, not JavaScript.
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
        this->m_filteredDegrees = 0.0;
        this->m_displayDegrees = 0.0;
        this->m_filteredVelocity = 0.0;
    }

    bool initialized() const { return this->m_initialized; }
    double targetDegrees() const { return this->m_filteredDegrees; }
    double displayDegrees() const { return this->m_displayDegrees; }

    // Return false for invalid or out-of-order input, leaving state unchanged.
    bool sample(double rawDegrees, double monotonicSeconds)
    {
        const double normalized = normalize(rawDegrees);
        if (normalized < 0.0 || !std::isfinite(monotonicSeconds))
            return false;

        if (!this->m_initialized)
        {
            this->m_initialized = true;
            this->m_lastSampleSeconds = monotonicSeconds;
            this->m_filteredDegrees = normalized;
            this->m_displayDegrees = normalized; // first fix appears immediately
            this->m_filteredVelocity = 0.0;
            return true;
        }

        const double elapsed = monotonicSeconds - this->m_lastSampleSeconds;
        if (elapsed <= 0.0)
            return false;

        // Resets on a discontinuous sensor stream rather than processing a
        // reading against stale filter history after sleep or a long stall.
        if (elapsed > 1.0)
        {
            this->reset();
            return this->sample(normalized, monotonicSeconds);
        }

        const double dt = std::max(elapsed, 0.001);
        const double displacement = shortestDelta(this->m_filteredDegrees, normalized);
        const double speed = displacement / dt;
        const double velocityAlpha = smoothingAlpha(dt, 1.0); // 1 Hz derivative cutoff
        this->m_filteredVelocity += velocityAlpha * (speed - this->m_filteredVelocity);

        // Tunable One Euro parameters for magnetic azimuth, in degrees.
        // Slow or stationary: ~1.5 Hz suppresses magnetic jitter.
        // Fast turning: 0.018 * degrees/sec increases cutoff to reduce lag.
        const double cutoffHz = 1.5 + 0.018 * std::fabs(this->m_filteredVelocity);
        const double alpha = smoothingAlpha(dt, cutoffHz);
        this->m_filteredDegrees = normalize(this->m_filteredDegrees + alpha * displacement);
        this->m_lastSampleSeconds = monotonicSeconds;
        return true;
    }

    // Advance the visible heading with wall-clock delta independently of the
    // sensor sampling rate. This is a time-based first-order response, not an
    // animation restarted on each reading. Never extrapolate beyond a sample.
    // Returns whether the visible dial moved by a perceptible fraction.
    bool advanceDisplay(double elapsedSeconds)
    {
        if (!this->m_initialized || !std::isfinite(elapsedSeconds) || elapsedSeconds <= 0.0)
            return false;
        const double dt = std::min(elapsedSeconds, 0.1);
        const double difference = shortestDelta(this->m_displayDegrees, this->m_filteredDegrees);
        if (std::fabs(difference) < 0.01)
        {
            this->m_displayDegrees = this->m_filteredDegrees;
            return false;
        }
        // ~40ms time constant fills gaps between 20Hz sensor packets at 60Hz.
        const double fraction = 1.0 - std::exp(-dt / 0.040);
        const double change = difference * fraction;
        this->m_displayDegrees = normalize(this->m_displayDegrees + change);
        return std::fabs(change) >= 0.005;
    }

private:
    static double smoothingAlpha(double seconds, double cutoffHz)
    {
        constexpr double tauFactor = 6.2831853071795864769; // 2*pi
        const double quantity = tauFactor * cutoffHz * seconds;
        return quantity / (1.0 + quantity);
    }

    bool m_initialized = false;
    double m_lastSampleSeconds = 0.0;
    double m_filteredDegrees = 0.0;
    double m_displayDegrees = 0.0;
    double m_filteredVelocity = 0.0;
};
}
