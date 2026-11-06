# Ignition on Android

The Android host is `src/platform/android.c`, on the NDK: a
NativeActivity through `android_native_app_glue`, an EGL OpenGL ES 3.0
context, keys, gamepads and touch from the input queue, and AAudio. The
APK has no Java code. It needs Android 8.0 (API 26) or later, on arm64
or x86_64.

## Build

`build.sh` uses the SDK's command-line tools directly; there is no
Gradle project. You need:

- the Android SDK, with `ANDROID_HOME` (or `ANDROID_SDK_ROOT`) set to it;
- in the SDK: an NDK, build-tools 35 or later and the `android-34`
  platform, for example
  `sdkmanager "ndk;27.2.12479018" "build-tools;35.0.0" "platforms;android-34"`;
- a JDK (17 or later, for `apksigner` and `keytool`), CMake, Ninja and
  Python 3 (which draws the icon).

```sh
make android            # build-android/ignition.apk
make android-install    # adb install
make android-clean
```

The version comes from the `VERSION` file, or from the argument to
`build.sh`. It sets `versionName` and `versionCode`: 0.1.0 is code 100.

### Signing

Set `ANDROID_KEYSTORE`, `ANDROID_KEYSTORE_PASS` and `ANDROID_KEY_ALIAS` to
sign with your release key. Without them, `build.sh` makes a debug key in
`build-android/`. An APK signed with the debug key installs, but cannot
update an APK signed with a different key.

## Game data

Start the app once, so that its folder exists. Then copy the disc image
of your own copy of the game (the `.cue` file and its `.bin` files) into

```
Android/data/io.github.xesf.ignition/files/
```

over USB from a computer, or with adb:

```sh
adb push *.cue *.bin /sdcard/Android/data/io.github.xesf.ignition/files/
```

At the next start, the game copies the files out of the image into
`disc/` in the same folder. A `disc/` folder with the CD's files works
too.

## Controls

Touch the screen once, and an on-screen pad shows: a d-pad on the left,
A, B, X and Y on the right, Select and Start at the top. The buttons do
what a gamepad's do (see [../linux/README.md](../linux/README.md#controls)).
Gamepads and keyboards also work. The Back button is Escape.

The small move icon at the bottom of the screen opens the pad's editor:
drag a button to move it, tap one to pick it. Three sliders set the size
(of the picked button, or of all of them when none is picked), how
see-through the buttons are and how thick their rings are. The bin hides
the picked button (a hidden button shows faintly in the editor, and a tap
brings it back), the circular arrow restores the defaults and the check
mark keeps the layout in `touchpad.cfg`. The game gets no input while the
editor is open.

If the game closes by itself, `crash.txt` in the same folder has the
signal and the calls, and `Default.fil` the game's own log.
