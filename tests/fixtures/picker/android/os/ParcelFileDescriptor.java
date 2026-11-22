package android.os;

import java.io.ByteArrayInputStream;

public class ParcelFileDescriptor {
    private final byte[] content;
    public ParcelFileDescriptor(byte[] content) { this.content = content; }

    public static class AutoCloseInputStream extends ByteArrayInputStream {
        public AutoCloseInputStream(ParcelFileDescriptor fd) { super(fd.content); }
    }
}
