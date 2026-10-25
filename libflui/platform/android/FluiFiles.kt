package org.skstu.libflui

import android.app.Activity
import android.content.ClipData
import android.content.Intent
import android.net.Uri
import android.provider.DocumentsContract
import android.provider.OpenableColumns
import android.os.Handler
import android.os.Looper
import android.webkit.MimeTypeMap
import androidx.core.content.FileProvider
import io.flutter.plugin.common.MethodChannel
import org.json.JSONArray
import java.io.File
import java.lang.ref.WeakReference
import java.util.Locale
import java.util.UUID
import java.util.concurrent.Executors

// Process-owned state survives Activity recreation; all completion is on main.
object FluiFiles {
    private const val requestCode=43907
    private var activity=WeakReference<Activity>(null)
    private var pending: MethodChannel.Result?=null
    private var source: File?=null
    private var selectionLimit=0
    private val main=Handler(Looper.getMainLooper())
    private val io=Executors.newSingleThreadExecutor()
    fun attach(owner: Activity, channel: MethodChannel) {
        activity=WeakReference(owner)
        channel.setMethodCallHandler { call,result ->
            if(pending!=null){result.success(mapOf("status" to 6,"path" to ""));return@setMethodCallHandler}
            if(call.method=="pick_file" || call.method=="pick_media") {
                val current=activity.get()
                if(current==null){result.success(mapOf("status" to 7,"path" to ""));return@setMethodCallHandler}
                pending=result;source=null
                selectionLimit=if(call.method=="pick_media" && call.argument<Boolean>("multiple")==true)9 else 1
                try {
                    current.startActivityForResult(Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
                        addCategory(Intent.CATEGORY_OPENABLE);type="*/*"
                        addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
                        putExtra(Intent.EXTRA_ALLOW_MULTIPLE,selectionLimit>1)
                        if(call.method=="pick_media")putExtra(Intent.EXTRA_MIME_TYPES,arrayOf("image/*","video/*"))
                    },requestCode)
                } catch(_:Exception){complete(7,"")}
                return@setMethodCallHandler
            }
            val path=call.argument<String>("source")?:""
            val name=call.argument<String>("name")?:""
            val file=File(path)
            if(!file.isAbsolute || !file.isFile || name.isEmpty() || name.contains('/') || name.contains('\\')){
                result.success(mapOf("status" to 1,"path" to ""));return@setMethodCallHandler
            }
            val current=activity.get()
            if(current==null){result.success(mapOf("status" to 7,"path" to ""));return@setMethodCallHandler}
            pending=result;source=file
            val mime=if(name.endsWith(".apk",ignoreCase=true)) "application/vnd.android.package-archive"
                else MimeTypeMap.getSingleton().getMimeTypeFromExtension(name.substringAfterLast('.',"").lowercase(Locale.ROOT))?:"application/octet-stream"
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
                                    setDataAndType(uri,mime);clipData=ClipData.newRawUri(name,uri)
                                    addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
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
        val result=pending;pending=null;source=null;selectionLimit=0
        result?.success(mapOf("status" to status,"path" to path))
    }
    fun result(code:Int,outcome:Int,data:Intent?):Boolean {
        if(code!=requestCode)return false
        val owner=activity.get()
        if(selectionLimit>0) {
            if(outcome!=Activity.RESULT_OK){complete(0,"");return true}
            val clip=data?.clipData
            val count=clip?.itemCount?:if(data?.data!=null)1 else 0
            if(count>selectionLimit){complete(1,"");return true}
            if(count==0 || owner==null){complete(7,"");return true}
            val uris=if(clip!=null)(0 until count).map { clip.getItemAt(it).uri } else listOf(data!!.data!!)
            val multiple=selectionLimit>1
            val application=owner.applicationContext
            io.execute {
                val files=mutableListOf<File>()
                try {
                    val resolver=application.contentResolver
                    for(selected in uris) {
                        val name=try {
                            resolver.query(selected,arrayOf(OpenableColumns.DISPLAY_NAME),null,null,null)?.use {
                                if(it.moveToFirst())it.getString(0) else null
                            }
                        } catch(_:Exception){null}
                        val extension=if(name==null)MimeTypeMap.getSingleton().getExtensionFromMimeType(resolver.getType(selected)) else null
                        val input=resolver.openInputStream(selected)?:throw java.io.IOException("Selected stream unavailable")
                        files.add(FluiFileImport.copy(input,File(application.cacheDir,"libflui-picked"),
                            name?:if(extension==null)"selected-file" else "selected-file.$extension"))
                    }
                    val paths=files.map { it.absolutePath }
                    val path=if(multiple)JSONArray(paths).toString() else paths.single()
                    main.post { complete(0,path) }
                } catch(_:Exception) {
                    for(file in files)file.parentFile?.deleteRecursively()
                    main.post { complete(7,"") }
                }
            }
            return true
        }
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
