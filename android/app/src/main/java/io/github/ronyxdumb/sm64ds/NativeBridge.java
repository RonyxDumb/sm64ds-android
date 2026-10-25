package io.github.ronyxdumb.sm64ds;
import java.io.IOException;
final class NativeBridge {
    static { System.loadLibrary("sm64ds_android"); }
    static native String probe(String path) throws IOException;
    private NativeBridge() {}
}
