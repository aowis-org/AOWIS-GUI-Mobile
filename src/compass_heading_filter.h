#pragma once

#include "compass_heading.h"

#include <algorithm>
#include <cmath>

// Adaptive circular heading filter (a "One Euro" filter on the unwrapped
// angle). Its cut-off frequency rises with the estimated turn rate:
//
//  * holding still  -> low cut-off: magnetic noise is averaged away, and the
//                      estimate keeps converging on the true mean heading;
//  * turning        -> high cut-off: little lag, the rose follows the turn.
//
// The previous observer instead *froze* the estimate until a departure of
// ~2.6 deg was confirmed. That looked calm but left a persistent offset of
// up to that size, moved slow turns in visible steps, and lagged turns by up
// to ~13 deg. Here no measurement is ever discarded by a gate; only the
// rendered needle has a tiny deadband (hundredths of a degree) so a
// stationary phone does not trigger endless subpixel repaints.
namespace AowisCompass
{
inline double shortestDelta(double fromDegrees, double toDegrees)
{
    return std::remainder(toDegrees - fromDegrees, 360.0);
}

struct HeadingFilterTuning
{
    double minCutoffHz;          // cut-off while still: lower = calmer, slower
    double beta;                 // cut-off increase per deg/s of turn rate
    double rateCutoffHz;         // smoothing of the turn-rate estimate
    double displayTimeConstant;  // seconds; bridges sensor steps at frame rate
    double stillRateDegrees;     // |turn rate| below this counts as "not moving"
    double fullLeadRateDegrees;  // lead compensation fades in up to this rate
    double leadMarginDegrees;    // lead may pass the last measurement by this
    double displayDeadband;      // degrees; stop repainting closer than this
};

// Android fused rotation vector: gyroscope-stabilised, already smooth.
inline constexpr HeadingFilterTuning kFusedSensorTuning{1.5, 0.05, 1.5, 0.015, 3.0, 12.0, 0.3, 0.03};
// AOWIS fusion without gyroscope (accelerometer + magnetometer only):
// much noisier, so average harder when still.
inline constexpr HeadingFilterTuning kRawCompassTuning{0.35, 0.04, 1.0, 0.05, 6.0, 25.0, 1.0, 0.06};

class HeadingFilter final
{
public:
    void setTuning(const HeadingFilterTuning &tuning) { this->m_tuning = tuning; }
    const HeadingFilterTuning &tuning() const { return this->m_tuning; }

    void reset()
    {
        this->m_initialized = false;
        this->m_lastSampleSeconds = 0.0;
        this->m_lastFrameSeconds = -1.0;
        this->m_estimateDegrees = 0.0;
        this->m_rate = 0.0;
        this->m_turnRate = 0.0;
        this->m_measuredDegrees = 0.0;
        this->m_displayDegrees = 0.0;
    }

    bool initialized() const { return this->m_initialized; }
    bool moving() const { return std::fabs(this->m_turnRate) >= this->m_tuning.stillRateDegrees; }
    double targetDegrees() const { return normalize(this->m_estimateDegrees); }
    double continuousTargetDegrees() const { return this->m_estimateDegrees; }
    double displayDegrees() const { return normalize(this->m_displayDegrees); }
    double continuousDisplayDegrees() const { return this->m_displayDegrees; }
    double velocityDegreesPerSecond() const { return this->m_turnRate; }

    bool sample(double rawDegrees, double monotonicSeconds)
    {
        const double measured = normalize(rawDegrees);
        if (measured < 0.0 || !std::isfinite(monotonicSeconds))
            return false;
        if (!this->m_initialized)
        {
            this->m_initialized = true;
            this->m_lastSampleSeconds = monotonicSeconds;
            this->m_estimateDegrees = measured;
            this->m_measuredDegrees = measured;
            this->m_displayDegrees = measured;
            this->m_rate = 0.0;
            this->m_turnRate = 0.0;
            return true;
        }

        const double dt = monotonicSeconds - this->m_lastSampleSeconds;
        if (dt <= 0.0)
            return false;
        if (dt > 1.0)
        {
            // A long gap: the old estimate says nothing about the present.
            // Keep the display angle continuous, restart the estimate.
            const double display = this->m_displayDegrees;
            this->m_estimateDegrees = display + shortestDelta(normalize(display), measured);
            this->m_measuredDegrees = this->m_estimateDegrees;
            this->m_rate = 0.0;
            this->m_turnRate = 0.0;
            this->m_lastSampleSeconds = monotonicSeconds;
            return true;
        }

        // Unwrap relative to the current estimate: handles north crossings
        // in either direction, any number of times.
        const double unwrapped = this->m_estimateDegrees +
                                 shortestDelta(normalize(this->m_estimateDegrees), measured);
        this->m_measuredDegrees = unwrapped;

        const double observedRate = (unwrapped - this->m_estimateDegrees) / dt;
        this->m_rate += smoothing(this->m_tuning.rateCutoffHz, dt) * (observedRate - this->m_rate);
        this->m_rate = std::clamp(this->m_rate, -720.0, 720.0);

        const double cutoff = this->m_tuning.minCutoffHz + this->m_tuning.beta * std::fabs(this->m_rate);
        const double previousEstimate = this->m_estimateDegrees;
        this->m_estimateDegrees += smoothing(cutoff, dt) * (unwrapped - this->m_estimateDegrees);

        // The residual-based rate above is deliberately quick but noisy; it
        // only adapts the cut-off. The derivative of the *filtered* estimate
        // is far quieter and equals the true rate in a steady turn: it drives
        // the motion state and the display lead.
        const double estimateRate = (this->m_estimateDegrees - previousEstimate) / dt;
        this->m_turnRate += smoothing(this->m_tuning.rateCutoffHz, dt) * (estimateRate - this->m_turnRate);
        this->m_lastSampleSeconds = monotonicSeconds;
        return true;
    }

    bool advanceDisplay(double monotonicSeconds)
    {
        if (!this->m_initialized || !std::isfinite(monotonicSeconds))
            return false;
        if (this->m_lastFrameSeconds < 0.0 || monotonicSeconds < this->m_lastFrameSeconds)
        {
            this->m_lastFrameSeconds = monotonicSeconds;
            return false;
        }
        const double dt = std::min(monotonicSeconds - this->m_lastFrameSeconds, 0.1);
        this->m_lastFrameSeconds = monotonicSeconds;
        if (dt <= 0.0)
            return false;

        // Lead compensation: during a steady turn, both the adaptive
        // low-pass and the display follower lag by (rate x time constant).
        // Aim ahead by exactly that; the term fades with the rate estimate
        // as soon as the turn stops, so a stationary needle is unaffected.
        const double cutoff = this->m_tuning.minCutoffHz + this->m_tuning.beta * std::fabs(this->m_rate);
        const double speed = std::fabs(this->m_turnRate);
        const double fade = std::clamp((speed - this->m_tuning.stillRateDegrees) /
            std::max(1e-6, this->m_tuning.fullLeadRateDegrees - this->m_tuning.stillRateDegrees), 0.0, 1.0);
        double lead = fade * fade * this->m_turnRate *
            (this->m_tuning.displayTimeConstant + 1.0 / (2.0 * kPi * cutoff));
        // Never aim beyond what the sensor has actually reported (plus a
        // noise margin): when a turn stops, the needle settles onto the
        // measured heading instead of swinging past it and back.
        const double ahead = this->m_measuredDegrees - this->m_estimateDegrees;
        if (lead > 0.0)
            lead = std::min(lead, std::max(0.0, ahead + this->m_tuning.leadMarginDegrees));
        else if (lead < 0.0)
            lead = std::max(lead, std::min(0.0, ahead - this->m_tuning.leadMarginDegrees));
        const double error = this->m_estimateDegrees + lead - this->m_displayDegrees;
        if (!moving() && std::fabs(error) < this->m_tuning.displayDeadband)
            return false;
        // First-order follower: frame-rate independent, cannot overshoot.
        const double gain = 1.0 - std::exp(-dt / this->m_tuning.displayTimeConstant);
        const double previous = this->m_displayDegrees;
        this->m_displayDegrees += gain * error;
        return std::fabs(this->m_displayDegrees - previous) >= 0.002;
    }

private:
    static double smoothing(double cutoffHz, double dt)
    {
        return 1.0 - std::exp(-2.0 * kPi * cutoffHz * dt);
    }

    HeadingFilterTuning m_tuning = kFusedSensorTuning;
    bool m_initialized = false;
    double m_lastSampleSeconds = 0.0;
    double m_lastFrameSeconds = -1.0;
    double m_estimateDegrees = 0.0; // unwrapped
    double m_rate = 0.0;            // deg/s, residual based (cut-off only)
    double m_turnRate = 0.0;        // deg/s, from the filtered estimate
    double m_measuredDegrees = 0.0; // latest measurement, unwrapped
    double m_displayDegrees = 0.0;  // unwrapped
};
}
