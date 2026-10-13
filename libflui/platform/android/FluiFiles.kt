package org.skstu.libflui

import android.app.Activity
import android.content.Intent
import android.net.Uri
import android.provider.DocumentsContract
import android.webkit.MimeTypeMap
import androidx.core.content.FileProvider
import io.flutter.plugin.common.MethodChannel
import java.io.File
import java.lang.ref.WeakReference
import java.util.UUID
import java.util.concurrent.Executors

// Process-owned state survives Activity recreation; all completion is on main.
object FluiFiles {
    private const val requestCode=43907
    private var activity=WeakReference<Activity>(null)
    private var pending: MethodChannel.Result?=null
    private var source: File?=null
    private val io=Executors.newSingleThreadExecutor()
    fun attach(owner: Activity, channel: MethodChannel) {
        activity=WeakReference(owner)
        channel.setMethodCallHandler { call,result ->
            if(pending!=null){result.success(mapOf("status" to 6,"path" to ""));return@setMethodCallHandler}
            val path=call.argument<String>("source")?:""
            val name=call.argument<String>("name")?:""
            val file=File(path)
            if(!file.isAbsolute || !file.isFile || name.isEmpty() || name.contains('/') || name.contains('\\')){
                result.success(mapOf("status" to 1,"path" to ""));return@setMethodCallHandler
            }
            val current=activity.get()
            if(current==null){result.success(mapOf("status" to 7,"path" to ""));return@setMethodCallHandler}
            pending=result;source=file
            val mime=MimeTypeMap.getSingleton().getMimeTypeFromExtension(name.substringAfterLast('.',""))?:"application/octet-stream"
            if(call.method=="export") {
                try {
                    current.startActivityForResult(Intent(Intent.ACTION_CREATE_DOCUMENT).apply {
                        addCategory(Intent.CATEGORY_OPENABLE);type=mime;putExtra(Intent.EXTRA_TITLE,name)
                    },requestCode)
                } catch(_:Exception){complete(7,"")}
            } else if(call.method=="view") {
                val application=current.applicationContext
                io.execute {
                    try {
                        val copy=File(File(application.cacheDir,"libflui-open/${UUID.randomUUID()}"),name)
                        copy.parentFile!!.mkdirs();file.copyTo(copy,overwrite=false,bufferSize=65536)
                        val uri=FileProvider.getUriForFile(application,application.packageName+".libflui.files",copy)
                        current.runOnUiThread {
                            try {
                                activity.get()?.startActivity(Intent(Intent.ACTION_VIEW).apply {
                                    setDataAndType(uri,mime);addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
                                })?:throw IllegalStateException()
                                complete(0,uri.toString())
                            } catch(_:android.content.ActivityNotFoundException){complete(10,"")}
                              catch(_:Exception){complete(7,"")}
                        }
                    } catch(_:Exception){current.runOnUiThread{complete(7,"")}}
                }
            } else complete(1,"")
        }
    }
    private fun complete(status:Int,path:String) {
        val result=pending;pending=null;source=null
        result?.success(mapOf("status" to status,"path" to path))
    }
    fun result(code:Int,outcome:Int,data:Intent?):Boolean {
        if(code!=requestCode)return false
        val owner=activity.get()
        val uri:Uri?=data?.data
        if(outcome!=Activity.RESULT_OK || uri==null){complete(0,"");return true}
        val file=source
        if(owner==null || file==null){complete(7,"");return true}
        val resolver=owner.applicationContext.contentResolver
        io.execute {
            var status=0
            try {
                resolver.openOutputStream(uri,"w")!!.use { out ->
                    file.inputStream().use { input -> input.copyTo(out,65536) };out.flush()
                }
            } catch(_:Exception) {
                status=7
                try {DocumentsContract.deleteDocument(resolver,uri)}catch(_:Exception){}
            }
            owner.runOnUiThread { complete(status,if(status==0)uri.toString() else "") }
        }
        return true
    }
}
