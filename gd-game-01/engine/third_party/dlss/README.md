# NVIDIA DLSS SDK (downloaded, not in the repo)

`tools/fetch_dlss.sh` fills this folder from NVIDIA's official repository
(https://github.com/NVIDIA/DLSS), pinned to one release and checked by SHA-256:
the NGX headers, the NGX static library and the DLSS model library (57 MB),
plus `LICENSE.txt` (NVIDIA RTX SDKs licence). `--windows` adds the MSVC
library and DLL.

Without these files the game still builds (`GD01_DLSS=0`) and runs: the AI
upscaler falls back to FSR 3.1, then FSR 1.
