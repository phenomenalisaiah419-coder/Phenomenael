# v0.22 Hook Dispatch

The five verified hook bindings now have a typed runtime dispatcher:

- Tick -> `ClientAbiTickRunner`
- ReadState -> `ClientAbiAdapter::invoke_read_state`
- ReadInventory -> `ClientAbiAdapter::invoke_read_inventory`
- ReadTouch -> `ClientAbiAdapter::invoke_read_touch`
- SubmitActions -> `ClientAbiAdapter::invoke_submit_actions`

The dispatcher is fail-closed when detached and records successful and rejected dispatches atomically.

The exact Mojang ARM64 ABI is still not guessed. A modified client must translate its verified 1.26.50 function ABI into `ClientAbiTable` and/or the complete hook installer contract.
