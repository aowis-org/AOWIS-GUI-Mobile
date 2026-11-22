package android.content;
import android.net.Uri;
public class ClipData {
    private final Item[] items;
    public ClipData(Item... items) { this.items = items; }
    public int getItemCount() { return items.length; }
    public Item getItemAt(int index) { return items[index]; }
    public static class Item {
        private final Uri uri;
        public Item(Uri uri) { this.uri = uri; }
        public Uri getUri() { return uri; }
    }
}
