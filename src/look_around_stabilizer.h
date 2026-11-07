#pragma once

#include <QQuaternion>

#include <algorithm>
#include <cmath>

namespace AowisMobileMap
{
// Filter the device pose before applying screen orientation and geographic north.
inline QQuaternion stabilizeLookAround(const QQuaternion &previous, const QQuaternion &measured,
                                       double magnification, double elapsed_seconds)
{
    if (magnification <= 1.0 || elapsed_seconds <= 0.0 || elapsed_seconds > 0.25)
        return measured;

    const QQuaternion delta = previous.conjugated() * measured;
    const double angle = 2.0 * std::atan2(double(delta.vector().length()),
                                          std::abs(double(delta.scalar())));
    // Small excursions get stronger damping as the lens narrows. A sustained
    // turn opens the response as the measurement pulls away from the estimate.
    constexpr double radians_per_degree = 0.017453292519943295;
    const double turn = std::clamp((angle / radians_per_degree - 0.35) / 3.0, 0.0, 1.0);
    const double blend = turn * turn * (3.0 - 2.0 * turn);
    const double zoom_fraction = std::clamp(std::log2(magnification) / 3.0, 0.0, 1.0);
    const double time_constant = zoom_fraction * (0.30 * (1.0 - blend) + 0.022 * blend);
    const float weight = float(-std::expm1(-elapsed_seconds / time_constant));
    return QQuaternion::slerp(previous, measured, weight).normalized();
}
}
