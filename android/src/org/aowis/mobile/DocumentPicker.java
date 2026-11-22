package org.aowis.mobile;

import android.content.ClipData;
import android.content.Context;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import android.provider.OpenableColumns;

import java.io.ByteArrayOutputStream;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;

/**
 * Android's system document picker (ACTION_OPEN_DOCUMENT) for importing a
 * text file, and reading the picked document through the ContentResolver.
 * Started from C++ with QtAndroidPrivate::startActivity(); the picked URI
 * carries a temporary read grant, which is all reading needs.
 */
public final class DocumentPicker {
    private DocumentPicker() { }

    /** Intent for one openable document of any type (.conf has no MIME type). */
    public static Intent openDocumentIntent() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        intent.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, false);
        return intent;
    }

    /** The picked document of an ACTION_OPEN_DOCUMENT result, or null. */
    public static Uri resultUri(Intent data) {
        if (data == null || data.getData() != null) {
            return data == null ? null : data.getData();
        }
        ClipData clip = data.getClipData();
        return clip != null && clip.getItemCount() == 1 ? clip.getItemAt(0).getUri() : null;
    }

    /** Read once under the picker's temporary grant. The status contains no provider or file data. */
    public static final class ReadResult {
        public final String status;
        public final String text;

        private ReadResult(String status, String text) {
            this.status = status;
            this.text = text;
        }
    }

    public static ReadResult readText(Context context, Uri uri, int maxBytes) {
        if (context == null || uri == null || maxBytes < 0) {
            return new ReadResult("invalid reader arguments", null);
        }
        ReadResult first;
        try (InputStream in = context.getContentResolver().openInputStream(uri)) {
            first = readStream(in, maxBytes);
        } catch (SecurityException e) {
            return new ReadResult("denied", null);
        } catch (IOException | RuntimeException e) {
            first = new ReadResult(e.getClass().getSimpleName(), null);
        }
        if ("ok".equals(first.status) || "tooLarge".equals(first.status)) {
            return first;
        }
        // Some content providers expose a raw file descriptor even when opening
        // their asset stream fails. Start again from byte zero on a new handle.
        try {
            ParcelFileDescriptor fd = context.getContentResolver().openFileDescriptor(uri, "r");
            if (fd != null) {
                try (InputStream in = new ParcelFileDescriptor.AutoCloseInputStream(fd)) {
                    ReadResult second = readStream(in, maxBytes);
                    if ("ok".equals(second.status) || "tooLarge".equals(second.status)) {
                        return second;
                    }
                    return failure(first.status, second.status);
                }
            }
            return failure(first.status, "no descriptor");
        } catch (SecurityException e) {
            return new ReadResult("denied", null);
        } catch (FileNotFoundException e) {
            if ("FileNotFoundException".equals(first.status)) {
                return new ReadResult("notFound", null);
            }
            return failure(first.status, e.getClass().getSimpleName());
        } catch (IOException | RuntimeException e) {
            return failure(first.status, e.getClass().getSimpleName());
        }
    }

    private static ReadResult failure(String first, String second) {
        // Exception messages may contain the URI. Report only the failure type.
        return new ReadResult("stream: " + first + ", descriptor: " + second, null);
    }

    private static ReadResult readStream(InputStream in, int maxBytes) throws IOException {
        if (in == null) {
            return new ReadResult("no stream", null);
        }
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        byte[] buffer = new byte[8192];
        while (true) {
            int count = in.read(buffer, 0, (int) Math.min(buffer.length, (long) maxBytes - out.size() + 1));
            if (count < 0) {
                return new ReadResult("ok", new String(out.toByteArray(), StandardCharsets.UTF_8));
            }
            if (count == 0) {
                return new ReadResult("zero-byte read", null);
            }
            if (count > maxBytes - out.size()) {
                return new ReadResult("tooLarge", null);
            }
            out.write(buffer, 0, count);
        }
    }

    /** The document's display name ("wg-office.conf"), or null. */
    public static String displayName(Context context, Uri uri) {
        if (context == null || uri == null) {
            return null;
        }
        try (Cursor cursor = context.getContentResolver().query(
                uri, new String[] { OpenableColumns.DISPLAY_NAME }, null, null, null)) {
            if (cursor != null && cursor.moveToFirst()) {
                return cursor.getString(0);
            }
        } catch (Exception e) {
            // No name: the caller falls back to a generic one.
        }
        return null;
    }
}
