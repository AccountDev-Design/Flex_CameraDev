package com.flex.cameradev.ui

import android.content.Intent
import android.os.Bundle
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.flex.cameradev.R

/** Explains what the app does and does not do, and opens the diagnostics screen. */
class InfoActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_info)
        requireViewById<TextView>(R.id.infoCloseButton).setOnClickListener { finish() }
        requireViewById<TextView>(R.id.infoDiagnosticsButton).setOnClickListener {
            startActivity(Intent(this, DiagnosticsActivity::class.java))
        }
    }
}
