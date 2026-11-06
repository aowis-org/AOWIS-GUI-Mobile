package android.content;
import android.content.pm.PackageManager;
import android.hardware.camera2.CameraManager;
public class Context {
    public static final String CAMERA_SERVICE = "camera";
    public final PackageManager pm = new PackageManager();
    public final CameraManager manager = new CameraManager();
    public PackageManager getPackageManager() { return pm; }
    public Object getSystemService(String name) { return CAMERA_SERVICE.equals(name) ? manager : null; }
}
