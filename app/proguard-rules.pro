# CameraX and Camera2 interop rely on reflection for extension/interop lookups.
-keep class androidx.camera.** { *; }
-dontwarn androidx.camera.**

# Keep the pure-logic core intact so stack traces stay readable in release builds.
-keep class com.flex.cameradev.core.** { *; }
