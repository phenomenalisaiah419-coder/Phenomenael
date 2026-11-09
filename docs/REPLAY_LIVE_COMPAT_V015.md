# Replay + Live Compatibility — v0.15

## Replay ingestion

`ReplayTransportBackend` loads sanitized recorder files and replays clientbound logical Bedrock frames through the normal transport/session seam. It can also capture serverbound observations for deterministic comparison.

## Live compatibility runner

`live_compat_runner` is a compiled executable for actual 1.26.50 server testing. It requires externally supplied legitimate credentials and never generates Microsoft/Xbox credentials. Required environment variables are:

- `BEDROCKAI_SERVER=host:port`
- `BEDROCKAI_LOGIN_CHAIN_FILE=/path/to/login-chain.json`
- `BEDROCKAI_CLIENT_DATA_FILE=/path/to/client-data.json`
- `BEDROCKAI_MULTIPLAYER_TOKEN_FILE=/path/to/token.jwt` for modern auth
- optional `BEDROCKAI_AUTH_TYPE=0|2`

A successful live run is defined as the real server reaching the existing secure runtime `WorldReady` state. The runner is not registered as a default CTest because this environment has no legitimate Microsoft/Xbox credential material.
