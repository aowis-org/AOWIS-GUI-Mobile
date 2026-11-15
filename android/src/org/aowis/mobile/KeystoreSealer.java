package org.aowis.mobile;

import android.security.keystore.KeyGenParameterSpec;
import android.security.keystore.KeyProperties;

import java.nio.charset.StandardCharsets;
import java.security.Key;
import java.security.KeyStore;
import java.util.Arrays;
import java.util.Base64;

import javax.crypto.Cipher;
import javax.crypto.KeyGenerator;
import javax.crypto.SecretKey;
import javax.crypto.spec.GCMParameterSpec;

/**
 * Seals WireGuard secrets (private and preshared keys) with an AES-256-GCM
 * key held by the Android Keystore. The key never leaves the Keystore and
 * is not part of backups: sealed values restored on another device or after
 * a reinstall cannot be opened, and unseal() then returns null.
 *
 * Token: "aowis-keystore-v1:" + base64url(12-byte IV || ciphertext || tag),
 * the prefix authenticated as associated data. Never logs values.
 */
public final class KeystoreSealer {
    static final String PREFIX = "aowis-keystore-v1:";
    private static final String ALIAS = "aowis-wireguard-secrets-v1";
    private static final int TAG_BITS = 128;
    private static final int IV_BYTES = 12;

    /** Source of the sealing key; replaced in host tests (no Android Keystore there). */
    interface KeySource {
        SecretKey key(boolean create) throws Exception;
    }

    static KeySource keySource = KeystoreSealer::androidKey;

    private KeystoreSealer() { }

    /** Sealed token for plain, or null when the Keystore cannot seal. */
    public static String seal(String plain) {
        if (plain == null) {
            return null;
        }
        try {
            SecretKey key = keySource.key(true);
            if (key == null) {
                return null;
            }
            Cipher cipher = Cipher.getInstance("AES/GCM/NoPadding");
            // The Keystore chooses the IV (randomized encryption is required).
            cipher.init(Cipher.ENCRYPT_MODE, key);
            byte[] iv = cipher.getIV();
            if (iv == null || iv.length != IV_BYTES) {
                return null;
            }
            cipher.updateAAD(PREFIX.getBytes(StandardCharsets.US_ASCII));
            byte[] sealed = cipher.doFinal(plain.getBytes(StandardCharsets.UTF_8));
            byte[] payload = new byte[iv.length + sealed.length];
            System.arraycopy(iv, 0, payload, 0, iv.length);
            System.arraycopy(sealed, 0, payload, iv.length, sealed.length);
            return PREFIX + Base64.getUrlEncoder().withoutPadding().encodeToString(payload);
        } catch (Exception e) {
            return null;
        }
    }

    /** Plain value of a token from seal(), or null (wrong key, damaged, not a token). */
    public static String unseal(String token) {
        if (token == null || !token.startsWith(PREFIX)) {
            return null;
        }
        try {
            byte[] payload = Base64.getUrlDecoder().decode(token.substring(PREFIX.length()));
            if (payload.length <= IV_BYTES + TAG_BITS / 8) {
                return null;
            }
            SecretKey key = keySource.key(false);
            if (key == null) {
                return null;
            }
            Cipher cipher = Cipher.getInstance("AES/GCM/NoPadding");
            cipher.init(Cipher.DECRYPT_MODE, key,
                    new GCMParameterSpec(TAG_BITS, Arrays.copyOfRange(payload, 0, IV_BYTES)));
            cipher.updateAAD(PREFIX.getBytes(StandardCharsets.US_ASCII));
            byte[] plain = cipher.doFinal(payload, IV_BYTES, payload.length - IV_BYTES);
            return new String(plain, StandardCharsets.UTF_8);
        } catch (Exception e) {
            return null;
        }
    }

    private static SecretKey androidKey(boolean create) throws Exception {
        KeyStore store = KeyStore.getInstance("AndroidKeyStore");
        store.load(null);
        Key existing = store.getKey(ALIAS, null);
        if (existing instanceof SecretKey) {
            return (SecretKey) existing;
        }
        if (!create) {
            return null;
        }
        KeyGenerator generator = KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, "AndroidKeyStore");
        generator.init(new KeyGenParameterSpec.Builder(ALIAS,
                KeyProperties.PURPOSE_ENCRYPT | KeyProperties.PURPOSE_DECRYPT)
                .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
                .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
                .setKeySize(256)
                .setRandomizedEncryptionRequired(true)
                .build());
        return generator.generateKey();
    }
}
