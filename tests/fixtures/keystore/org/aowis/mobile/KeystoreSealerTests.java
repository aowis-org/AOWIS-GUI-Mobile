package org.aowis.mobile;

import javax.crypto.KeyGenerator;
import javax.crypto.SecretKey;

public class KeystoreSealerTests {
    public static void main(String[] args) throws Exception {
        KeyGenerator generator = KeyGenerator.getInstance("AES");
        generator.init(256);
        final SecretKey key = generator.generateKey();
        final boolean[] present = {false};
        KeystoreSealer.keySource = create -> {
            if (create) present[0] = true;
            return present[0] ? key : null;
        };
        final String secret = "yAnz5TF+lXXJte14tji3zlMNq+hd2rYUIgJBgB3fBmk=";
        if (KeystoreSealer.unseal(KeystoreSealer.PREFIX + "AAAA") != null) throw new AssertionError("no key yet: nothing opens");
        String sealed = KeystoreSealer.seal(secret);
        if (sealed == null || !sealed.startsWith(KeystoreSealer.PREFIX) || sealed.contains(secret))
            throw new AssertionError("sealed token");
        if (!sealed.matches("[A-Za-z0-9:_-]+")) throw new AssertionError("token must be a single .conf value");
        if (!secret.equals(KeystoreSealer.unseal(sealed))) throw new AssertionError("round trip");
        if (sealed.equals(KeystoreSealer.seal(secret))) throw new AssertionError("random IV per seal");
        char[] tampered = sealed.toCharArray();
        int last = tampered.length - 1;
        tampered[last] = tampered[last] == 'A' ? 'B' : 'A';
        if (KeystoreSealer.unseal(new String(tampered)) != null) throw new AssertionError("tampering detected");
        if (KeystoreSealer.unseal(secret) != null || KeystoreSealer.unseal(null) != null) throw new AssertionError("not a token");
        // Another device / reinstall: a different Keystore key never opens it.
        KeyGenerator other = KeyGenerator.getInstance("AES");
        other.init(256);
        final SecretKey foreign = other.generateKey();
        KeystoreSealer.keySource = create -> foreign;
        if (KeystoreSealer.unseal(sealed) != null) throw new AssertionError("foreign key must fail");
        KeystoreSealer.keySource = create -> { throw new IllegalStateException("keystore unavailable"); };
        if (KeystoreSealer.seal(secret) != null) throw new AssertionError("failure must not fall back to plain");
        System.out.println("PASS: Keystore sealer round trip, random IVs, tamper and foreign-key rejection, fail closed");
    }
}
