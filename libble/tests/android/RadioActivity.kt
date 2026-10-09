package com.skstu.devkit.blelab
import android.app.Activity
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.content.Intent
import android.content.Context
import android.content.BroadcastReceiver
import android.widget.TextView
import com.skstu.devkit.ble.BleRuntime
class RadioActivity : Activity() {
    override fun onCreate(state: Bundle?){
        super.onCreate(state)
        val status=BleRuntime.initialize(this)
        val label=TextView(this);label.text="libble 射频验证\n仅 BLE-TEST 合成数据\n等待显式测试指令\n初始化: $status";label.textSize=22f;label.setPadding(20,60,20,20);setContentView(label)
    }
}
class RadioControl: BroadcastReceiver(){
    companion object {init {System.loadLibrary("ble_radio_lab")}; private val main=Handler(Looper.getMainLooper()); private var generation=0}
    private external fun nativeStart(role: String, round: String, seconds: Int, warm: Int): Int
    private external fun nativeStop()
    override fun onReceive(context: Context, intent: Intent){
        if(intent.action!="com.skstu.devkit.blelab.RUN")return
        val role=intent.getStringExtra("role") ?: return
        val round=intent.getStringExtra("round") ?: return
        val seconds=intent.getIntExtra("seconds",0)
        val warm=intent.getIntExtra("warm",0)
        if(role !in setOf("scan","advertise") || !Regex("20261010-r[0-9]+").matches(round) || seconds !in 5..180 || warm !in 0..3 || (warm!=0 && role!="scan") || BleRuntime.initialize(context)!=0)return
        nativeStop();val epoch=++generation
        fun start(attempt: Int){if(epoch!=generation)return; if(nativeStart(role,round,seconds,warm)==-3 && attempt<100)main.postDelayed({start(attempt+1)},50)}
        start(0)
    }
}
