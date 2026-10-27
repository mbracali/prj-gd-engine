/* SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
 * Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
 * stb_image (PNG, JPEG) and stb_truetype (third_party/stb, public domain /
 * MIT, unmodified): the launcher's images and fonts. */
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#include "stb_image.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
