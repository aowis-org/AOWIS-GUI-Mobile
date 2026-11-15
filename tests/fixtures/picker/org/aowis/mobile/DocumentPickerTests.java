package org.aowis.mobile;
import android.content.Context;
import android.content.Intent;
import android.net.Uri;
import java.nio.charset.StandardCharsets;
public class DocumentPickerTests {
    public static void main(String[] args) {
        Intent intent = DocumentPicker.openDocumentIntent();
        if (!Intent.ACTION_OPEN_DOCUMENT.equals(intent.action) || !Intent.CATEGORY_OPENABLE.equals(intent.category)
                || !"*/*".equals(intent.type) || !Boolean.FALSE.equals(intent.multiple))
            throw new AssertionError("single openable document of any type");
        Intent result = new Intent();
        Uri uri = new Uri("content://com.android.providers.downloads.documents/document/msf%3A42");
        result.data = uri;
        if (DocumentPicker.resultUri(result) != uri || DocumentPicker.resultUri(null) != null) throw new AssertionError("result uri");
        Context context = new Context();
        if (DocumentPicker.readText(context, uri, 100) != null) throw new AssertionError("unreadable must be null");
        context.resolver.content = "[Interface]\nPrivateKey = x\n".getBytes(StandardCharsets.UTF_8);
        if (!"[Interface]\nPrivateKey = x\n".equals(DocumentPicker.readText(context, uri, 100))) throw new AssertionError("text");
        context.resolver.content = new byte[50000];
        String large = DocumentPicker.readText(context, uri, 10000);
        if (large == null || large.length() <= 10000 || large.length() > 20000) throw new AssertionError("stops after the limit");
        if (DocumentPicker.displayName(context, uri) != null) throw new AssertionError("no name");
        context.resolver.name = "wg-office.conf";
        if (!"wg-office.conf".equals(DocumentPicker.displayName(context, uri))) throw new AssertionError("display name");
        if (DocumentPicker.readText(null, uri, 1) != null || DocumentPicker.displayName(context, null) != null) throw new AssertionError("null input");
        System.out.println("PASS: document picker intent, result URI, ContentResolver text with size limit, display name");
    }
}
