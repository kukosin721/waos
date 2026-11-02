@file:Suppress("unused", "UNUSED_PARAMETER", "ClassName")

package android.content

/** Stubs de tipado (no van al APK). Sólo lo que necesitan los archivos que se verifican. */
open class Context {
    val filesDir: java.io.File = java.io.File("/tmp")
    val applicationContext: Context get() = this
    val contentResolver: ContentResolver = ContentResolver()
    fun getSystemService(name: String): Any? = null
    fun registerReceiver(receiver: BroadcastReceiver?, filter: IntentFilter): Intent? = null
    fun registerReceiver(receiver: BroadcastReceiver?, filter: IntentFilter, flags: Int): Intent? = null
    fun unregisterReceiver(receiver: BroadcastReceiver) = Unit

    companion object {
        const val USB_SERVICE = "usb"
        const val RECEIVER_NOT_EXPORTED = 4
    }
}

open class Intent(action: String? = null) {
    val action: String? = action
    @Suppress("UNCHECKED_CAST")
    fun <T> getParcelableExtra(name: String): T? = null
    fun <T : android.os.Parcelable> getParcelableExtra(name: String, clazz: Class<T>): T? = null
    fun getBooleanExtra(name: String, defaultValue: Boolean): Boolean = defaultValue
}

class IntentFilter {
    fun addAction(action: String) = Unit
}

abstract class BroadcastReceiver {
    abstract fun onReceive(context: Context, intent: Intent)
}

class ComponentName(pkg: String, cls: String)

class ContentResolver {
    fun openFileDescriptor(uri: android.net.Uri, mode: String): android.os.ParcelFileDescriptor? = null
}
