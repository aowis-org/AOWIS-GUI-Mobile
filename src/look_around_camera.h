#pragma once

#include "look_around_vision.h"

#include <QObject>
#include <QString>

#include <atomic>
#include <functional>
#include <memory>

// Back camera frames for Look around's camera stabilization, straight from
// the Android NDK Camera2 API (no Qt Multimedia). About 640x480 YUV at up to
// 30 fps; only the luma plane is used. Each frame is tracked on the camera's
// own callback thread (VisionTracker: a few thousand pixel samples per frame)
// and the result is delivered on the thread owning this object.
//
// Frame times are on the sensor clock (CLOCK_BOOTTIME, like the motion
// sensors): taken as they are when the camera reports its timestamps as
// "realtime", otherwise converted from CLOCK_MONOTONIC.
//
// The CAMERA permission must be granted before start(). Not on Android:
// start() fails.
class LookAroundCamera final : public QObject
{
public:
    explicit LookAroundCamera(QObject *parent = nullptr);
    ~LookAroundCamera() override;

    // Called on this object's thread for every processed frame.
    void setResultHandler(std::function<void(const AowisMobileMap::VisionTracker::Result &)> handler);

    bool start();
    void stop();
    bool active() const;
    // Empty while running normally; otherwise why the camera is not used.
    QString problem() const;
    // Raw image rotation reported by the driver (degrees), -1 if unknown.
    int reportedOrientation() const;

    struct Impl;

private:
    std::function<void(const AowisMobileMap::VisionTracker::Result &)> m_handler;
    std::unique_ptr<Impl> m_impl;
    std::atomic<unsigned int> m_generation{0};
    QString m_problem;
    int m_reportedOrientation = -1;
};
