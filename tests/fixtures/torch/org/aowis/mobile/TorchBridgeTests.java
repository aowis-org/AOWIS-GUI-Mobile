package org.aowis.mobile;
import android.content.Context;
public class TorchBridgeTests {
    public static void main(String[] args) {
        Context context = new Context();
        if (!TorchBridge.isAvailable(context)) throw new AssertionError("flash expected");
        if (!TorchBridge.setEnabled(context, true)) throw new AssertionError("enable failed");
        if (!"rear".equals(context.manager.lastId)) throw new AssertionError("wrong camera");
        if (!Boolean.TRUE.equals(context.manager.lastMode)) throw new AssertionError("not enabled");
        if (!TorchBridge.setEnabled(context, false)) throw new AssertionError("disable failed");
        if (!Boolean.FALSE.equals(context.manager.lastMode)) throw new AssertionError("not disabled");
        context.manager.fail = true;
        if (TorchBridge.setEnabled(context, true)) throw new AssertionError("busy not detected");
        context.manager.fail = false;
        context.pm.flash = false;
        if (TorchBridge.isAvailable(context)) throw new AssertionError("unavailable not detected");
        if (TorchBridge.isAvailable(null) || TorchBridge.setEnabled(null, true))
            throw new AssertionError("null context not rejected");
        System.out.println("PASS: torch Java bridge selects rear flash, toggles, and rejects failures");
    }
}
