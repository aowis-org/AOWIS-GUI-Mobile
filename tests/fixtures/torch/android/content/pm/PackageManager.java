package android.content.pm;
public class PackageManager {
    public static final String FEATURE_CAMERA_FLASH = "android.hardware.camera.flash";
    public boolean flash = true;
    public boolean hasSystemFeature(String feature) { return flash && FEATURE_CAMERA_FLASH.equals(feature); }
}
