# JNI upcalls are invisible to R8. Preserve only the named bridge contract;
# the rest of the engine and application may still be shrunk and optimized.
-keep class com.skstu.devkit.ble.BleEngine {
    public <init>(android.content.Context, long, java.lang.String, java.lang.String, java.lang.String, boolean);
    public int command(int, long, java.lang.String, java.lang.String, byte[], long);
    public int close();
    public int setOptions(int[]);
    public int setReadInterval(int);
    private native <methods>;
}
-keep class com.skstu.devkit.ble.BleRuntime {
    private native <methods>;
}
