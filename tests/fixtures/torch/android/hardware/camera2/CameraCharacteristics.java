package android.hardware.camera2;
public class CameraCharacteristics {
    public static final int LENS_FACING_BACK = 1;
    public static final Object FLASH_INFO_AVAILABLE = new Object();
    public static final Object LENS_FACING = new Object();
    public boolean flash = true;
    public int facing = LENS_FACING_BACK;
    @SuppressWarnings("unchecked")
    public <T> T get(Object key) {
        return (T) (key == FLASH_INFO_AVAILABLE ? Boolean.valueOf(flash) : Integer.valueOf(facing));
    }
}
