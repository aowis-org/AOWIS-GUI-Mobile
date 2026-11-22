package org.aowis.mobile;
import android.content.Context;
import android.content.ClipData;
import android.content.Intent;
import android.net.Uri;
import java.nio.charset.StandardCharsets;
public class DocumentPickerTests {
    public static void main(String[] args) {
        Intent intent = DocumentPicker.openDocumentIntent();
        if (!Intent.ACTION_OPEN_DOCUMENT.equals(intent.action) || !Intent.CATEGORY_OPENABLE.equals(intent.category)
                || (intent.flags & Intent.FLAG_GRANT_READ_URI_PERMISSION) == 0
                || !"*/*".equals(intent.type) || !Boolean.FALSE.equals(intent.multiple))
            throw new AssertionError("single openable document of any type");
        Intent result = new Intent();
        Uri uri = new Uri("content://com.android.providers.downloads.documents/document/msf%3A42");
        result.data = uri;
        if (DocumentPicker.resultUri(result) != uri || DocumentPicker.resultUri(null) != null) throw new AssertionError("result uri");
        result.data = null;
        result.clip = new ClipData(new ClipData.Item(uri));
        if (DocumentPicker.resultUri(result) != uri) throw new AssertionError("single clip selection");
        result.clip = new ClipData(new ClipData.Item(uri), new ClipData.Item(uri));
        if (DocumentPicker.resultUri(result) != null) throw new AssertionError("ambiguous selection");
        Context context = new Context();
        if (!"notFound".equals(DocumentPicker.readText(context, uri, 100).status)) throw new AssertionError("missing file");
        context.resolver.nullStream = true;
        String failure = DocumentPicker.readText(context, uri, 100).status;
        if (!"stream: no stream, descriptor: FileNotFoundException".equals(failure))
            throw new AssertionError("safe diagnostic for both provider paths");
        context.resolver.nullStream = false;
        context.resolver.descriptorContent = "test".getBytes(StandardCharsets.UTF_8);
        if (!"test".equals(DocumentPicker.readText(context, uri, 4).text)) throw new AssertionError("descriptor fallback");
        if (!"tooLarge".equals(DocumentPicker.readText(context, uri, 3).status)) throw new AssertionError("descriptor size limit");
        int requests = context.resolver.descriptorRequests;
        context.resolver.denied = true;
        if (!"denied".equals(DocumentPicker.readText(context, uri, 100).status)) throw new AssertionError("permission denied");
        if (requests != context.resolver.descriptorRequests) throw new AssertionError("retry after access denied");
        context.resolver.denied = false;
        context.resolver.content = "[Interface]\nPrivateKey = x\n".getBytes(StandardCharsets.UTF_8);
        if (!"[Interface]\nPrivateKey = x\n".equals(DocumentPicker.readText(context, uri, 100).text)) throw new AssertionError("text");
        context.resolver.content = new byte[50000];
        DocumentPicker.ReadResult large = DocumentPicker.readText(context, uri, 10000);
        if (!"tooLarge".equals(large.status) || large.text != null) throw new AssertionError("stops after the limit");
        if (DocumentPicker.displayName(context, uri) != null) throw new AssertionError("no name");
        context.resolver.name = "wg-office.conf";
        if (!"wg-office.conf".equals(DocumentPicker.displayName(context, uri))) throw new AssertionError("display name");
        if (!"invalid reader arguments".equals(DocumentPicker.readText(null, uri, 1).status) || DocumentPicker.displayName(context, null) != null) throw new AssertionError("null input");
        System.out.println("PASS: document picker intent, result URI, ContentResolver text with size limit, display name");
    }
}
