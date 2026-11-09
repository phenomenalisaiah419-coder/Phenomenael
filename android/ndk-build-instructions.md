# Android NDK build — v0.15

The production Android target remains an embedded/custom Bedrock client build. The controller core is compiled as reusable C++ and exposed through `libbedrockai_jni.so`.

```bash
cmake -S android -B build-android \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_ROOT/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-26 \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-android --target bedrockai_jni
```

CI pins Android NDK `30.0.16248370` and builds with warnings treated as errors. The current environment does not ship an Android NDK, so the ARM64 binary is not claimed as locally linked. The JNI/provider translation units are nevertheless compiled against host JNI headers with Android defines and strict warnings.
