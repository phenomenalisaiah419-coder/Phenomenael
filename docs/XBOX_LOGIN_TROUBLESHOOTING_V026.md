# Microsoft / Xbox 401 Recovery — v0.26

## What was wrong with the older path

The older `tools/xbox_login.py` completed Microsoft's device-code step, then tried the classic Xbox `user.authenticate` → `xsts.authorize` sequence. A successful Microsoft device-code login proves only that Microsoft issued an OAuth token; it does not prove that Xbox produced the right title/device/Bedrock multiplayer claims.

v0.26 replaces that Xbox leg with the Bedrock Android flow:

1. Microsoft Live Connect device-code login (or refresh using the cached refresh token).
2. Xbox device authentication using a stable P-256 proof key and device ID.
3. SISU authorization for Bedrock Android title `0000000048183522`, requesting UserToken, TitleToken, and the default Xbox authorization token.
4. XSTS authorization with DeviceToken + TitleToken + UserToken and the exact multiplayer relying party `https://multiplayer.minecraft.net/`.
5. Minecraft Bedrock `/authentication` request with the resulting `XBL3.0 x=<userhash>;<XSTS>` authorization header and a fresh P-384 identity public key.
6. Construction of the 1.26.50 / protocol 2193 Login payload with signed ClientData.

The signatures use XAL's signed-request format: policy version, Windows-epoch timestamp, method, path/query, Authorization header, and body; ECDSA-SHA256 on P-256; raw `r||s` appended to the version/timestamp prefix and standard-Base64 encoded. The requests include `x-xbl-contract-version: 1`.

## Run it in Termux

```sh
pkg update
pkg install python clang cmake make openssl
python -m pip install --upgrade pip
python -m pip install requests cryptography PyJWT
cd /path/to/bedrock_ai_controller_26_50_v0_26
python tools/xbox_login.py SERVER_HOST 19132
```

Use the actual server host and port in both places. When Microsoft displays a device code, complete that exact sign-in in a browser using the Microsoft account that owns Minecraft. If you already have a refresh token in the private cache, the script attempts a refresh before requesting a new code.

## Interpret failures by stage

The script identifies the failing stage and, when supplied by Xbox, reports the `XErr` code without printing any access token, Xbox token, refresh token, or key:

- `Microsoft device-code` / `Microsoft token polling`: the OAuth device-code request, account consent, code, or refresh flow failed.
- `Xbox device authentication`: XASD rejected the device proof, key, timestamp, or device token request.
- `Xbox SISU authorization`: Xbox could not exchange the MSA + device token for the title/user tokens. Confirm the app/title configuration and account Xbox profile/permissions if an XErr is given.
- `Bedrock multiplayer XSTS`: the underlying tokens were not accepted for the multiplayer relying party.
- `Minecraft authentication-chain request`: Xbox auth succeeded far enough to request the Minecraft Bedrock chain, but the Minecraft endpoint rejected the chain request.

A generic HTTP 401 alone is not enough to identify account ownership, privacy settings, or which claim was rejected; preserve the stage and `XErr`/reason text, but never share tokens or the cache directory.

## Private cache files

The default is `$HOME/.bedrock_ai` (override with `BEDROCK_AI_CACHE`). The directory is set to mode 0700 where supported, and generated files are set to 0600 where supported. Files include a Microsoft refresh token, Xbox P-256 device key, Xbox device token, P-384 Bedrock identity key, full Login payload, and profile metadata. Treat the whole directory as a password-equivalent secret. Do not upload it to chats, repositories, issue trackers, or public storage.

The C++ `FileLoginCacheProvider` enforces the matching server endpoint and protocol, validates the Login envelope, and verifies the P-384 private/public key pair. The current Android `connect_raknet` path does not yet launch `Bedrock2193SecureRuntime`, so this cache loader is currently an integration seam—not a claim that the Android app can already import a Termux cache and log into a live server.

## Verification status

- Automated auth protocol tests use mocked responses and no real Microsoft/Xbox account.
- The actual sign-in flow must still be tested from the user's Termux environment.
- The exact target server may impose its own allow-list, account permissions, or proxy restrictions.
