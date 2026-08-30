package com.flex.cameradev.ui

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.os.Bundle
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.lifecycle.lifecycleScope
import com.flex.cameradev.R
import com.flex.cameradev.camera.CameraCapabilities
import com.flex.cameradev.camera.DiagnosticsReport
import com.flex.cameradev.horizon.SensorFusionManager
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

/** Shows the real camera metadata so results can be reported from the device. */
class DiagnosticsActivity : AppCompatActivity() {

    private lateinit var body: TextView
    private var reportText: String = ""

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_diagnostics)
        body = requireViewById(R.id.diagnosticsBody)
        body.text = getString(R.string.diagnostics_none)

        requireViewById<TextView>(R.id.diagnosticsCloseButton).setOnClickListener { finish() }
        requireViewById<TextView>(R.id.diagnosticsCopyButton).setOnClickListener { copyReport() }

        lifecycleScope.launch {
            val text = withContext(Dispatchers.IO) {
                val report = CameraCapabilities.probeDevice(applicationContext)
                val source = SensorFusionManager(applicationContext).source
                DiagnosticsReport.build(applicationContext, report, source)
            }
            reportText = text
            body.text = text
        }
    }

    private fun copyReport() {
        if (reportText.isBlank()) return
        val clipboard = getSystemService(Context.CLIPBOARD_SERVICE) as? ClipboardManager ?: return
        clipboard.setPrimaryClip(ClipData.newPlainText(getString(R.string.diagnostics_title), reportText))
        Toast.makeText(this, R.string.diagnostics_copied, Toast.LENGTH_SHORT).show()
    }
}
