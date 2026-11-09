# gd-engine-core/third_party

Vendored **single-header** libraries, unmodified, each in its own folder with
its licence file. Larger libraries are not copied here: they are fetched by
CMake at pinned versions (`../cmake/gdcore_deps.cmake`).

| Folder | Files | Version | Upstream commit | Licence | Used by |
|---|---|---|---|---|---|
| `stb/` | `stb_image.h`, `stb_image_write.h`, `stb_image_resize2.h` | 2.30, 1.16, 2.18 | nothings/stb `2c980bb59875b0d32144a71867fbdebb2f77cd20` (2026-08-02) | public domain or MIT (`stb/LICENSE`) | image: the `stb` backend |
| `bcdec/` | `bcdec.h` | 0.985 | iOrange/bcdec `80859ed3b7afb1c527a2a99d70c61457bea72d0c` (2026-09-02) | MIT or Unlicense (`bcdec/LICENSE`) | image: the `bcdec` backend |

The AUDIO area vendors `miniaudio/` here the same way.

Rules (docs/guidelines/engine-backend-guidelines.md, rule 6):

- Unmodified copies only; a fix goes upstream or into our wrapper code.
- The licence file next to the header; the packages carry it.
- Pin an exact upstream commit or release at least two weeks old, and write
  it in this table.
- The implementation is compiled once, in the area that uses it, with
  internal linkage (`*_STATIC`), so it never clashes with another copy in the
  same program (the launcher has its own `stb_image.h`; SDL3_image another).

Note on stb: upstream moved `stb_image_resize2.h` to its own repository on
2026-10-19 (nothings/stb `db69a22`). The pinned commit is the last one with all
three headers together; move `stb_image_resize2.h` to the new repository's
pinned release when updating.
