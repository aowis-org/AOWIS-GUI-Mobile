package android.hardware.camera2;
public class CameraManager {
    public String[] ids = {"front", "rear"};
    public boolean fail = false;
    public String lastId;
    public Boolean lastMode;
    public String[] getCameraIdList() throws CameraAccessException { return ids; }
    public CameraCharacteristics getCameraCharacteristics(String id) throws CameraAccessException {
        CameraCharacteristics characteristics = new CameraCharacteristics();
        if (id.equals("front")) characteristics.facing = 0;
        return characteristics;
    }
    public void setTorchMode(String id, boolean on) throws CameraAccessException {
        if (fail) throw new CameraAccessException("busy");
        lastId = id;
        lastMode = on;
    }
}
