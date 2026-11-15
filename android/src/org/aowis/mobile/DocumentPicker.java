package org.aowis.mobile;

import android.content.ContentResolver;
import android.content.Context;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.provider.OpenableColumns;

import java.io.ByteArrayOutputStream;
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
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        intent.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, false);
        return intent;
    }

    /** The picked document of an ACTION_OPEN_DOCUMENT result, or null. */
    public static Uri resultUri(Intent data) {
        return data == null ? null : data.getData();
    }

    /**
     * The document as UTF-8 text, at most maxBytes + 1 bytes read (so the
     * caller can tell "too large"), or null when it cannot be read.
     */
    public static String readText(Context context, Uri uri, int maxBytes) {
        if (context == null || uri == null) {
            return null;
        }
        ContentResolver resolver = context.getContentResolver();
        try (InputStream in = resolver.openInputStream(uri)) {
            if (in == null) {
                return null;
            }
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            byte[] buffer = new byte[8192];
            int read;
            while ((read = in.read(buffer)) > 0) {
                out.write(buffer, 0, read);
                if (out.size() > maxBytes) {
                    break;
                }
            }
            return new String(out.toByteArray(), StandardCharsets.UTF_8);
        } catch (Exception e) {
            return null;
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
