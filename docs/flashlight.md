# Flashlight

The fourth instrument tile in the hamburger menu switches the device's rear
camera LED torch on/off. The menu presents a 2×2 tool grid: Compass,
Gradienter, Plumb Bob and Flashlight. Map and Settings precede the tools;
Diagnostics follows a separator.

On Android, the C++ `FlashlightService` calls a small Java bridge using JNI.
The bridge uses Android `CameraManager.setTorchMode` without opening a camera
or recording images. It discovers an available flash camera and prefers the
rear-facing camera rather than assuming camera ID 0. The app only requests
`android.permission.CAMERA` after the user first attempts to switch on the
light. If the device has no flash, the permission is denied, or another app
uses the camera, a visible error is shown rather than a false ON indicator.

The flashlight stays on while AOWIS is active, even when the drawer closes or
the user switches pages. It is switched off on backgrounding or normal app
shutdown to avoid leaving the LED running unattended. It is not re-enabled
automatically on resume. The lit tile and label reflect successful AOWIS torch
requests, but Android may also disable a torch independently when the camera
becomes unavailable or another app takes control; Android does not grant this
app exclusive ownership. A full native TorchCallback is not yet implemented.

Linux does not pretend to provide phone hardware and reports the flashlight as
unavailable. Neither rendering nor Qt Multimedia are needed.

## Verification

`tests/test_android_torch_bridge.sh` compiles the production Java bridge
against test-only Android API stubs and verifies camera selection, on/off,
unavailable hardware and camera-busy errors. `tests/flashlight_service_tests.cpp`
checks that desktop service state never falsely reports a lit torch.
`tests/verify_flashlight_menu.py` validates the menu layout, native bridge and
Android permission/build integration. Actual camera permission and LED
behavior must be verified on a phone.
