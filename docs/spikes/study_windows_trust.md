# Windows: avoid SmartScreen and "Unknown publisher"

Status: study, 2026-10-28.

## Verdict

**Signing is possible, but for us it is costly and only partly effective.**
- **SignPath Foundation (free for open source) is closed to us.** It requires
  an OSI-approved licence for every component, and ours is PolyForm
  Noncommercial plus additional terms.
- **Microsoft Artifact Signing** (formerly Trusted Signing) is the cheapest
  public-trust option at **about US$9.99/month**. However, **individual
  developers must be in the US or Canada**. Organizations qualify in the
  EU/UK/etc. (not Brazil).
- A classic **OV certificate** on an HSM or a cloud key (about US$200 to 600 per
  year) works anywhere. Since 2024, neither OV nor EV skips SmartScreen
  instantly: reputation builds per certificate and file with download volume.

The recommendation is to **keep shipping unsigned for now** and to document the
"More info > Run anyway" step. Buy an OV cloud-signing certificate only when
download numbers justify it (about 2 to 3 days of work once bought).

## Facts that matter (2023+ rules)

- **CA/B Forum (June 2023):** every publicly trusted code-signing key (OV and EV)
  must live on a FIPS 140-2 L2 / CC EAL4+ HSM: a USB token or a cloud HSM.
  `.pfx` files in a repository secret are no longer possible.
- **EV no longer gives instant SmartScreen reputation.** Microsoft changed this in
  2024. EV now mainly matters for kernel drivers. So EV (about US$300 to 700 per
  year, a business entity required) is not worth it for us.
- **Artifact Signing:** Basic tier about US$9.99/month for 5,000 signatures
  (enough). It needs a paid Azure subscription. Public Trust is open to
  organizations in the US, CA, EU, UK, AU, NZ, JP, KR, SG, CH, NO and IL;
  **individuals only in the US/CA**. Identity is checked with a government ID
  (Verified ID / AU10TIX). Certificates last 3 days and are renewed automatically;
  timestamping keeps old signatures valid. No EV. SmartScreen reputation "builds
  up automatically" with download history. There is a GitHub Action
  (`azure/artifact-signing-action`).
- **SignPath Foundation:** free, signs as "SignPath Foundation". Conditions are an
  OSI licence with no commercial dual licensing, no proprietary parts, automated
  builds with origin verification, and a published code-signing policy. **Our
  licence fails the first condition.** SignPath.io's paid plans exist, but you
  bring your own certificate.
- **Reputation:** it is tracked per signing certificate and per file hash. Every
  release is a new hash. A stable signer carries reputation across releases;
  unsigned files start from zero each time. You can submit files to Microsoft
  (https://www.microsoft.com/wdsi/filesubmission) to speed up a false positive.

## What to sign

Everything Windows checks in the zip: `gdul.exe`, `SDL3.dll`, every
`gd-engine-games/*/gd-game-NN.dll`, FFmpeg's `av*.dll`, and
`libwinpthread-1.dll`. Unsigned DLLs don't trigger SmartScreen by themselves.
The prompt is about the `.exe` the user opens, coming from a zip with Mark of
the Web. Signing all of them still helps with antivirus heuristics and Smart App
Control (Windows 11), which **blocks unsigned DLLs** that have no reputation.
Always timestamp: `/tr <TSA> /td sha256 /fd sha256`.

## In GitHub Actions

Our Windows build cross-compiles in a Fedora container. Options:
- **Artifact Signing / cloud HSM via osslsigncode or jsign** (both run on
  Linux). Jsign supports Azure Artifact Signing, DigiCert KeyLocker, SSL.com
  eSigner, Google Cloud KMS and others. Run it in the Windows job after
  `verify_bundle.sh` and before zipping:
  `jsign --storetype TRUSTEDSIGNING --keystore weu.codesigning.azure.net --storepass "$TOKEN" --alias <account>/<profile> --tsaurl http://timestamp.acs.microsoft.com gdul.exe *.dll`
- Secrets: an OIDC federated credential (`azure/login` with no stored secret)
  or service-principal secrets. Skip signing when they are absent (forks).
- Cost: a few seconds per file. No extra jobs.

## Changes in our scripts

- `scripts/build.sh windows` and the release job: add a `sign_windows()` step
  (jsign) over every `.exe`/`.dll` when `GDUL_WIN_SIGN=1`.
- `verify_bundle.sh`: when signing is on, check signatures with
  `osslsigncode verify`.
- Release notes: until we sign, explain the SmartScreen "More info > Run
  anyway" step.

## Blockers

- Location and licence: the owner is in Brazil (no individual Artifact Signing),
  and the non-OSI licence rules out SignPath Foundation. Options: an OV cloud
  certificate in the owner's name (SSL.com eSigner, Certum, Sectigo + cloud key,
  about US$200 to 600 per year), or a legal entity in an eligible country.
- The certificate shows the owner's legal name as "Publisher".

## Sources

- Artifact Signing prerequisites (countries, individuals): https://learn.microsoft.com/en-us/azure/artifact-signing/quickstart
- Artifact Signing FAQ (no EV, SmartScreen reputation): https://learn.microsoft.com/en-us/azure/artifact-signing/faq
- Pricing: https://azure.microsoft.com/en-us/pricing/details/artifact-signing/
- Code-signing options overview: https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/code-signing-options
- SmartScreen reputation: https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/smartscreen-reputation
- SignPath Foundation conditions: https://signpath.org/terms
- CA/B Forum code-signing baseline (HSM keys): https://cabforum.org/working-groups/code-signing/requirements/
- jsign: https://ebourg.github.io/jsign/
