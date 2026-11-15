package android.security.keystore;
// Host-test stub: only compiled, never used (the test supplies its own key).
public final class KeyGenParameterSpec implements java.security.spec.AlgorithmParameterSpec {
    public static final class Builder {
        public Builder(String alias, int purposes) { }
        public Builder setBlockModes(String... modes) { return this; }
        public Builder setEncryptionPaddings(String... paddings) { return this; }
        public Builder setKeySize(int bits) { return this; }
        public Builder setRandomizedEncryptionRequired(boolean required) { return this; }
        public KeyGenParameterSpec build() { return new KeyGenParameterSpec(); }
    }
}
