# Bedrock Companion — Stage 1 foundation

This is a separate Android companion application for the existing Bedrock AI Controller C++ core. It does **not** join a Minecraft server and does **not** create a second player.

## Current scope

- User-started MediaProjection capture of the Android display.
- In-memory downsampling to a 160x90 grayscale frame (5 frames/second maximum); no screenshot file is saved and no network upload is implemented.
- Gesture-only AccessibilityService. It does not read the accessibility tree or window text.
- Short, manually triggered forward/look test gestures plus a small floating STOP/test panel when overlay permission is granted.
- Editable control coordinates expressed as percentages of the display.
- JNI safety self-check into the v0.35 C++ `ControlHub`; unknown server classification must remain observe-only, and Stage 1 does not pass unverified screen data into the hub or dispatch AI decisions to Minecraft.
- No screen observations are sent to the native hub in Stage 1. The self-check uses an unknown server classification, which must resolve to `observe_only`.
- A GitHub Actions workflow can build a debug APK using Android SDK 37, NDK 30.0.16248370, CMake 3.31.6 and Gradle 9.6.0.

## Build

The Android project is in this directory. Use JDK 17, Android SDK platform 37, CMake 3.31.6 and NDK 30.0.16248370.

```bash
gradle --no-daemon :app:assembleDebug
```

The APK is written to `app/build/outputs/apk/debug/app-debug.apk`.

The repository's CI workflow is `.github/workflows/android-companion-build.yml`. Run it manually with GitHub Actions or push a change to the watched paths; download the `bedrock-companion-debug-apk` artifact.

## First-device test

1. Install the debug APK.
2. Enable **Bedrock Companion touch controls** in Android Accessibility settings. It requests gesture dispatch only and does not request access to the accessibility tree.
3. Grant **Display over other apps** for the floating STOP/test panel.
4. Rotate the phone to landscape, start capture, and grant the screen-capture prompt. On Android versions that offer app-only or entire-screen capture, select **Entire screen** so Minecraft can be brought to the foreground.
5. Open Minecraft, join a private/local world, and use the floating FWD/TURN buttons briefly. If overlay permission was granted after capture began, returning to the companion app makes it request the overlay again.
6. If movement or camera points are misaligned, return to the companion app, edit the screen-percentage calibration fields, save, then reopen Minecraft.
7. Confirm that the screen frame counter advances. Run the native self-check; it must report `READY`, `policy=observe_only`, and `actions=none`. This verifies the v0.35 hub is linked and its default policy blocks unverified automation; it does not verify live game control.

## Safety and current limits

- Autonomy remains OFF. Only user-tapped, short test gestures are enabled.
- STOP in the floating panel or notification ends capture. The most recent 160x90 grayscale frame remains in RAM so the user can inspect it after returning to the app; it is cleared when a new capture session starts or the app process exits.
- The current frame analyzer only downsamples and measures luminance/motion. It does not yet recognise Minecraft HUD values, player coordinates, entities, hotbar contents, blocks or terrain.
- The native bridge currently runs a no-state `ControlHub` self-check only. It does not accept game observations. A validated perception adapter and verified same-character action path are later stages. It is therefore not yet safe to run continuous AI control.
- Gesture coordinates depend on the Minecraft HUD layout, screen rotation and device resolution; they require calibration on the actual phone.
- Use the first tests only in a private/local world. Server rules may prohibit automated play.
- A screen-capture grant is user initiated for every capture session. Android 14+ can require the media-projection foreground-service flow and can resize/stop the capture when the user changes what is shared.
