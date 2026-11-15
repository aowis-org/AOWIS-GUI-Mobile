package android.database;
public class Cursor implements AutoCloseable { private final String value;
    public Cursor(String value) { this.value = value; }
    public boolean moveToFirst() { return true; }
    public String getString(int column) { return value; }
    public void close() { } }
