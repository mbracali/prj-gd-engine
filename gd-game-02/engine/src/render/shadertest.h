// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

// gd02 --shadertest (gd01 lesson L5): compiles every shader program the
// renderer can build (the 2D program and every GH2 standard-shader
// permutation in StandardShaders::testKeys) on this driver, with no game
// data. OpenGL 3.3 core by default, OpenGL ES 3.0 with GD02_GLES=1.
// Returns 0 when all compile, 1 when any fails, 2 without a context.
int runShaderTest();
