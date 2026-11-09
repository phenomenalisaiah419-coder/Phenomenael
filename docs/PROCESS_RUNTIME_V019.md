# v0.19 — exact client-process runtime

v0.19 makes Android/native "attach" a real process-image preparation step instead of an object-existence check.

The runtime discovers the already-loaded `libminecraftpe.so` in the current process, validates ELF64 AArch64, obtains the GNU build ID and executable ranges, and optionally requires an exact build ID supplied by the host. It does not load a second arbitrary copy of the game library.

A prepared runtime may resolve exported symbols through the global process namespace, but resolution is accepted only when the resolved address lies inside an executable segment of the verified client image. Stripped/internal functions still require the version-specific signature profiles from `v0.18`/`v0.19`.

Hook plans use one line per hook:

`kind|name|build_id|signature|expected_bytes|patch_offset|patch_size`

Supported kinds are `Tick`, `ReadState`, `ReadInventory`, `ReadTouch`, and `SubmitActions`. Every hook in a plan must use the same build ID and a plan must contain exactly one Tick hook.

The Android JNI process-runtime handle exposes prepare/refresh/detach/status/report operations. Physical Tick activation still requires an exact 1.26.50 ARM64 client binary and its independently verified signature/expected instruction data.
