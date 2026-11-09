# v0.20 complete client hook plan

A complete plan contains exactly five entries for one build ID: `Tick`, `ReadState`, `ReadInventory`, `ReadTouch`, and `SubmitActions`.

The core resolves every signature only inside executable `libminecraftpe.so` segments and verifies expected original bytes before invoking the modified client exact-ABI installer. The installer receives the complete verified set in one callback.

The installer remains client-specific because the five game functions have different native ABIs and their exact addresses are build-specific. No guessed Mojang offsets are embedded in this project.

Once installed, the modified client should route its exact function hooks as follows:

* `Tick` calls the integration tick dispatch.
* `ReadState` publishes translated `ClientStateView` data to the integration.
* `ReadInventory` publishes translated inventory data.
* `ReadTouch` publishes the current multitouch/button state.
* `SubmitActions` consumes `EmbeddedActionFrame` after arbitration, preserving human gates and emergency-survival rules.

The existing `EmbeddedBedrockClientIntegration::tick()` remains the single-character control spine.


## v0.20 complete hook-plan binding
The integration validates one exact five-site plan (`Tick`, `ReadState`, `ReadInventory`, `ReadTouch`, `SubmitActions`) against the loaded build ID and executable segments, then passes the complete verified binding set to the modified client’s `install_client_hooks` callback. The callback is where the exact ABI-specific native hooks are installed; this project deliberately does not guess Mojang function signatures or offsets.
