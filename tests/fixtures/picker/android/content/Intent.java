package android.content;
import android.net.Uri;
public class Intent {
    public static final String ACTION_OPEN_DOCUMENT = "android.intent.action.OPEN_DOCUMENT";
    public static final String CATEGORY_OPENABLE = "android.intent.category.OPENABLE";
    public static final String EXTRA_ALLOW_MULTIPLE = "android.intent.extra.ALLOW_MULTIPLE";
    public String action, category, type; public Boolean multiple; public Uri data;
    public Intent() { }
    public Intent(String action) { this.action = action; }
    public Intent addCategory(String category) { this.category = category; return this; }
    public Intent setType(String type) { this.type = type; return this; }
    public Intent putExtra(String name, boolean value) { this.multiple = value; return this; }
    public static final int FLAG_GRANT_READ_URI_PERMISSION = 1;
    public int flags;
    public ClipData clip;
    public Intent addFlags(int flags) { this.flags |= flags; return this; }
    public ClipData getClipData() { return clip; }
    public Uri getData() { return data; }
}
