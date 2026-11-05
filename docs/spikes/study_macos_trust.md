# macOS: launch without Gatekeeper prompts

Status: study, 2026-10-28.

## Verdict

**Viable and worth doing once a Mac test is possible.** It costs **US$99 per
year** (Apple Developer Program, individual) and about **2 to 4 days** of script
and workflow work. Developer ID signing + hardened runtime + notarization +
stapling removes the "cannot be opened" prompt and App Translocation's worst
effects. We do **not** need `disable-library-validation` or JIT entitlements,
provided we sign every module and dylib with our own Team ID.

## What we ship today

`gd-engine-launcher/scripts/build.sh macos` (lines ~160-170, 475-481):
dylibbundler copies SDL3 into `Contents/Frameworks/`, and `install_name_tool`
rewrites FFmpeg and SDL paths. Then **ad-hoc** signing:
`codesign --force --sign -` on each module `.dylib`, and `codesign --force
--deep --sign -` on the app. The release notes tell users to right-click >
Open. On macOS 15+ that right-click bypass is gone: users must go to System
Settings > Privacy & Security > "Open Anyway". The launcher already works
around App Translocation (`SecTranslocateCreateOriginalPathForURL`).

## What is required

1. **Apple Developer Program** (individual, US$99/yr). Create a **Developer ID
   Application** certificate. Keep the private key safe. Developer ID
   certificates last 5 years, and a revoked one breaks every copy in the field.
2. **Sign inside-out, never `--deep`**. Apple discourages `--deep` for signing.
   Sign every Mach-O with our identity, hardened runtime and a secure timestamp:
   - `Contents/Frameworks/libSDL3.0.dylib`
   - every `Resources/gd-engine-games/**/*.dylib` (game modules, `libav*`,
     MoltenVK/`libvulkan` for gd01)
   - then the main executable, with entitlements, then the `.app`.
   ```sh
   codesign --force --timestamp --options runtime --sign "$DEVID" <dylib>
   codesign --force --timestamp --options runtime --entitlements gdul.entitlements --sign "$DEVID" "<app>"
   ```
   Note that Mach-O files under `Resources/` are allowed but unusual. Apple
   prefers code in `Contents/Frameworks` or `Contents/PlugIns`. Moving
   `gd-engine-games/*/*.dylib` to `Contents/PlugIns/gd-engine-games/` (with the
   yaml/json staying in `Resources`) avoids "code object is not signed at all"
   and seal problems. The launcher's module search path would then need
   one change.
3. **Entitlements** (`gdul.entitlements`):
   - **Library validation stays ON.** It only allows code signed by Apple or by
     *our* Team ID. All of our modules and FFmpeg/SDL builds are signed by us, so
     `dlopen` works. Use `com.apple.security.cs.disable-library-validation` only
     if we ever load third-party-signed modules. Apple advises against it, and it
     makes Gatekeeper stricter.
   - **No JIT** (`allow-jit` / `allow-unsigned-executable-memory`): we have no
     JIT. If a future dynarec arrives, add `com.apple.security.cs.allow-jit` and
     use `MAP_JIT`.
   - No sandbox (we don't go to the Mac App Store). So no file entitlements are
     needed. The TCC prompts for Downloads/Documents stay as they are.
4. **Notarize** with `notarytool`, using an App Store Connect **API key**
   (best for CI). Then **staple**:
   ```sh
   ditto -c -k --keepParent "Guitar Dream Universal Launcher.app" gdul.zip
   xcrun notarytool submit gdul.zip --key AuthKey.p8 --key-id $KID --issuer $ISS --wait
   xcrun stapler staple "Guitar Dream Universal Launcher.app"
   ```
   Then zip again for the release. Stapling covers offline first launch. A
   stapled, notarized app still gets translocated if it is quarantined and not
   moved. The existing translocation handling stays useful.
5. Check the result with `spctl -a -vv`, `codesign --verify --deep --strict`, and
   `stapler validate`.

## GitHub Actions

Repository secrets: `MACOS_CERT_P12_B64`, `MACOS_CERT_PASSWORD`,
`APPLE_API_KEY_P8_B64`, `APPLE_API_KEY_ID`, `APPLE_API_ISSUER`. In the `macos`
job of `gdul-release.yml`, create a temporary keychain (`security
create-keychain`, `import`, `set-key-partition-list`), sign, run notarytool
`--wait` (usually 1 to 10 minutes of macOS runner time, which bills at 10x),
staple, and delete the keychain. Skip signing (fall back to ad-hoc) when the
secrets are absent, so forks still build. The repo guard and gitleaks already
protect against leaked keys.

## Changes in our scripts

- `scripts/build.sh` macOS: replace the two ad-hoc `codesign` lines with a
  `sign_macos()` that uses `$GDUL_MACOS_IDENTITY` when set, otherwise `-`.
  Sign inside-out without `--deep`. Optionally move the modules to
  `Contents/PlugIns/`.
- Add `gd-engine-launcher/package/gdul.entitlements` (hardened runtime, empty or
  near-empty).
- `gdul-release.yml`: add keychain, notarize and staple steps. Update the
  release-notes row ("right-click > Open" is no longer needed).
- `verify_bundle.sh`: on macOS, `codesign --verify --strict` every Mach-O.

## Blockers and risks

- There is no Mac to test on yet ("not yet tried on a real Mac"). Notarization
  failures only show up on Apple's side, so the first run is the real test.
- gd01's module needs MoltenVK. Its dylib must be signed by us, and its ICD
  JSON must be in the bundle.
- The Developer ID certificate holder's legal name is shown to users.

## Sources

- Apple Developer Program fee: https://developer.apple.com/support/compare-memberships/
- Notarizing macOS software: https://developer.apple.com/documentation/security/notarizing-macos-software-before-distribution
- Hardened runtime / library validation: https://developer.apple.com/documentation/security/hardened-runtime , https://developer.apple.com/documentation/bundleresources/entitlements/com.apple.security.cs.disable-library-validation
- Placing content in a bundle: https://developer.apple.com/documentation/bundleresources/placing-content-in-a-bundle
- DTS on library loading under hardened runtime: https://developer.apple.com/forums/thread/706437
- macOS 15 Gatekeeper change (no Control-click bypass): https://developer.apple.com/news/?id=saqachfa
