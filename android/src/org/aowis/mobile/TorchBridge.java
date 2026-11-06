package org.aowis.mobile;

import android.content.Context;
import android.content.pm.PackageManager;
import android.hardware.camera2.CameraAccessException;
import android.hardware.camera2.CameraCharacteristics;
import android.hardware.camera2.CameraManager;
import android.util.Log;

/** Minimal torch bridge. Does not open the camera or capture images. */
public final class TorchBridge {
    private TorchBridge() { }

    public static boolean isAvailable(Context context) {
        if (context == null || !context.getPackageManager().hasSystemFeature(
                PackageManager.FEATURE_CAMERA_FLASH)) {
            return false;
        }
        // Hardware detection must work before CAMERA permission is granted.
        return context.getSystemService(Context.CAMERA_SERVICE) != null;
    }

    private static String findCamera(Context context) throws CameraAccessException {
        CameraManager manager = (CameraManager) context.getSystemService(Context.CAMERA_SERVICE);
        if (manager == null) {
            return null;
        }
        String fallback = null;
        for (String id : manager.getCameraIdList()) {
            CameraCharacteristics characteristics = manager.getCameraCharacteristics(id);
            if (!Boolean.TRUE.equals(characteristics.get(
                    CameraCharacteristics.FLASH_INFO_AVAILABLE))) {
                continue;
            }
            Integer facing = characteristics.get(CameraCharacteristics.LENS_FACING);
            if (facing != null && facing == CameraCharacteristics.LENS_FACING_BACK) {
                return id;
            }
            if (fallback == null) {
                fallback = id;
            }
        }
        return fallback;
    }

    public static boolean setEnabled(Context context, boolean enabled) {
        if (context == null) {
            return false;
        }
        try {
            CameraManager manager = (CameraManager) context.getSystemService(Context.CAMERA_SERVICE);
            String id = findCamera(context);
            if (manager == null || id == null) {
                return false;
            }
            manager.setTorchMode(id, enabled);
            return true;
        } catch (Exception exception) {
            Log.w("AOWIS Mobile", "Cannot change flashlight state", exception);
            return false;
        }
    }
}
