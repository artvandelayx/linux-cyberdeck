package com.linuxcyberdeck

import android.annotation.SuppressLint
import android.os.Bundle
import android.webkit.WebChromeClient
import android.webkit.WebView
import android.webkit.WebViewClient
import androidx.activity.ComponentActivity

class DesktopActivity : ComponentActivity() {
    private lateinit var desktop: WebView

    @SuppressLint("SetJavaScriptEnabled")
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        desktop = WebView(this).apply {
            settings.javaScriptEnabled = true
            settings.domStorageEnabled = true
            settings.allowFileAccess = false
            settings.allowContentAccess = false
            webViewClient = WebViewClient()
            webChromeClient = WebChromeClient()
            loadUrl("http://127.0.0.1:6080/vnc.html?autoconnect=1&resize=scale&host=127.0.0.1&port=6080")
        }
        setContentView(desktop)
    }

    override fun onDestroy() {
        desktop.destroy()
        super.onDestroy()
    }
}
