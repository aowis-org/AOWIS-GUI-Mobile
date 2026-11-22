package android.content;
import android.database.Cursor;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import java.io.ByteArrayInputStream;
import java.io.FileNotFoundException;
import java.io.InputStream;
public class ContentResolver {
    public boolean nullStream;
    public boolean denied; public byte[] content; public String name;
    public InputStream openInputStream(Uri uri) throws FileNotFoundException {
        if (nullStream) return null;
        if (denied) throw new SecurityException();
        if (content == null) throw new FileNotFoundException(uri.value);
        return new ByteArrayInputStream(content);
    }
    public byte[] descriptorContent;
    public int descriptorRequests;
    public ParcelFileDescriptor openFileDescriptor(Uri uri, String mode) throws FileNotFoundException {
        descriptorRequests++;
        if (!"r".equals(mode)) throw new AssertionError("read-only descriptor required");
        if (descriptorContent == null) throw new FileNotFoundException(uri.value);
        return new ParcelFileDescriptor(descriptorContent);
    }
    public Cursor query(Uri uri, String[] projection, String selection, String[] args, String order) {
        return name == null ? null : new Cursor(name);
    }
}
