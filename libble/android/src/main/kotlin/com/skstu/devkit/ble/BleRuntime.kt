package com.skstu.devkit.ble
import android.content.Context
/** Call once before creating a C ABI context. Does not access the radio or request permissions. */
object BleRuntime {
    init {System.loadLibrary("devkit_ble")}
    private external fun nativeInitialize(context: Context, engineClass: Class<*>): Int
    fun initialize(context: Context): Int = nativeInitialize(context.applicationContext,BleEngine::class.java)
}
