# Google Play: publish the Android launcher

Status: study, 2026-10-28.

## Verdict

**Technically close to ready. The open question is policy and IP, not code.**
Our APK already targets API 36, is arm64, is built with NDK r28 (16 KB pages)
and uses the system file picker (no storage permission). Play needs an **AAB**,
**Play App Signing**, a **US$25** account, the testing gate for new personal
accounts, and store forms. That is about **3 to 5 days of work plus at least 14
days of closed testing**. The real risk is an IP complaint or takedown over
"Guitar Hero" (Activision), because the app is useless without those discs.
Emulators with "bring your own files" are tolerated on Play, but a reimplementation
named after one franchise is a more visible target.

A related point: **Android developer verification is now in force in Brazil**
(since 2026-09-30, for certified devices). Sideloading our GitHub APK there now
needs either a verified developer registration or the user going through the
deliberately slow "advanced flow". Registering as a verified developer (via Play
Console or the new Android Developer Console) is worth doing **even without
Play**, before the global rollout in 2027.

## Checklist against Play's rules

| Requirement | Rule (2026) | Us today | Work |
|---|---|---|---|
| Account | Personal account, US$25 one-time, ID verification | none | Register. An organization account (D-U-N-S) would skip the testing gate. |
| Testing gate | New personal accounts (after 2023-11-13): a closed test with **12 opted-in testers for 14 continuous days** before Production. It was 20 at launch and 12 since late 2024. Reports say engagement is now checked. | n/a | Recruit 12+ testers (community/Discord) and keep them for 14 days |
| Package | **AAB** required for new apps | APK via Gradle | `./gradlew bundleRelease`. Keep the APK for GitHub. |
| Target API | New apps and updates: **API 36** (since 2026-08-31) | targetSdk 36, compileSdk 36 | none (bump yearly) |
| 64-bit | arm64-v8a required if native | arm64 only | none. Optional x86_64 for Chromebooks and emulators. |
| 16 KB pages | Native libs must be 16 KB aligned (targeting 35+) | NDK r28.2 (default 16 KB). FFmpeg uses `-z max-page-size=16384` | Verify with `zipalign -c -P 16` / `check_elf_alignment.sh`. |
| Signing | Play App Signing (Google holds the app key, we upload with an upload key) | release key in `ANDROID_GDUL_*` secrets | Enrol with **our existing key as the app signing key**, so GitHub APK users can update from Play and the other way round. Make a new upload key. |
| Native lib packaging | `useLegacyPackaging=true` (extracted libs) is allowed. It makes installs larger. | yes (modules `dlopen`ed from the native lib dir) | Keep it |
| Storage | `MANAGE_EXTERNAL_STORAGE` is restricted and needs approval | SAF picker + copy to app-specific `Android/data/.../original_isos/`. No storage permission. | none. Optional: open the `content://` fd directly (`/proc/self/fd/N`) to avoid a second 7 GB copy. |
| Data safety form | Required | No collection. `gdul.log` and the config stay on the device. No network except opening the website. | Declare "no data collected/shared" |
| Content rating | IARC questionnaire | n/a | Fill in. Expect Everyone/PEGI 3 to 7 (music, no violence). |
| Privacy policy URL | Required for all apps | none | Short page on guitardreamengine.com |
| Store listing | No third-party trademarks or impersonation in the title, icon or screenshots. Don't imply endorsement. | Our site says "Guitar Hero and Rock Band games" | Title "Guitar Dream Universal Launcher". Screenshots without Activision art (launcher list, model viewer with placeholder content). State in the description: requires your own discs, contains no game data. |

## Policy risk (bring-your-own-disc)

- Play allows emulators that do not ship copyrighted BIOS or ROMs (Dolphin,
  PPSSPP, RetroArch are listed). We ship no game data. Disc content (sounds) is
  read at run time and stays in memory, which fits that rule.
- The difference is that we are a **title-specific reimplementation**. An
  Activision DMCA or trademark complaint to Google could pull the listing and
  give the account a strike. **Mitigation:** a neutral name and art, a legal page
  (`docs/legal.md`), and no "Guitar Hero" in the title or short description.
- The "No sale, no paywall" terms (LICENSE-ADDITIONAL-TERMS B) fit a free app
  with no ads or IAP.

## GitHub Actions

Add `bundleRelease` to the Android job in `gdul-release.yml`, signed with an
**upload key** (new secrets `ANDROID_GDUL_UPLOAD_*`). Optionally upload to an
internal or closed track with `r0adkll/upload-google-play` (needs a Play service
account JSON secret). That adds about 1 minute and no new jobs.

## Steps

1. Register as a verified Android developer (needed for sideloading in Brazil anyway).
2. Create a Play personal account (US$25), a privacy policy page and the store listing text.
3. Gradle: add an AAB output and an upload key. Enrol in Play App Signing with the existing key.
4. Run a closed test with 12+ testers for 14 days. Fill in Data safety and IARC.
5. Apply for production, then automate uploads from the release workflow.

## Sources

- Target API requirement: https://developer.android.com/google/play/requirements/target-sdk , https://support.google.com/googleplay/android-developer/answer/11926878
- Testing requirement for new personal accounts: https://support.google.com/googleplay/android-developer/answer/14151465
- 16 KB page sizes: https://developer.android.com/guide/practices/page-sizes
- Play App Signing: https://support.google.com/googleplay/android-developer/answer/9842756
- All-files-access policy: https://support.google.com/googleplay/android-developer/answer/10467955
- Data safety: https://support.google.com/googleplay/android-developer/answer/10787469
- Developer verification (Brazil, 2026-09-30): https://www.androidauthority.com/android-sideloading-developer-verification-first-wave-rollout-3717921/ , https://developer.android.com/developer-verification
- Emulators on Play (secondary): https://www.pcworld.com/article/2378030
