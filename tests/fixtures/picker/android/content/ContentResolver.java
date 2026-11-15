package android.content;
import android.database.Cursor;
import android.net.Uri;
import java.io.ByteArrayInputStream;
import java.io.FileNotFoundException;
import java.io.InputStream;
public class ContentResolver {
    public byte[] content; public String name;
    public InputStream openInputStream(Uri uri) throws FileNotFoundException {
        if (content == null) throw new FileNotFoundException(uri.value);
        return new ByteArrayInputStream(content);
    }
    public Cursor query(Uri uri, String[] projection, String selection, String[] args, String order) {
        return name == null ? null : new Cursor(name);
    }
}
