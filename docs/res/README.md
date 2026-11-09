# Store and listing art

Art for launcher stores, game databases and press kits. Everything committed
here is our own art: the Guitar Dream Engine badge (`icon_full.jpg`), the
Metal Mania font (SIL OFL 1.1, bundled in
`gd-engine-launcher/res/fonts/`) and procedural renders. No game art.

| File | Size | What it is |
|---|---|---|
| `icon_full.jpg` | 2048 x 2048 | The GDE badge, square, black background (the source art) |
| `badge.png` | 1024 x 1027 | The badge cut out: transparent background, anti-aliased edge, the GDE letters' spikes kept |
| `logo.png` | 1200 x 242 | Horizontal logo: badge left, "GUITAR DREAM ENGINE" wordmark right, transparent |
| `logo@2x.png` | 2400 x 483 | The same, large master |
| `logo_stacked.png` | 800 x 803 | Stacked logo: badge above a two-line wordmark, transparent |
| `logo_stacked@2x.png` | 1600 x 1607 | The same, large master |
| `backdrop_1920x1080.png` | 1920 x 1080 | Full HD backdrop: perspective note highway with the five fret gems, stage beams, haze; the left side is kept dark for a store's text |
| `backdrop_1920x1080_logo.png` | 1920 x 1080 | The backdrop with the horizontal logo on the left |

The wordmark follows the launcher header and the site's `h1`: Metal Mania,
the fire gradient `#ffd34d` > `#ff8a1f` > `#e8343a`, a dark outline, a hard
black drop shadow and a soft orange glow.

## Regenerate

```
python3 res/tools/make_store_art.py
```

Python 3 with Pillow and numpy. It rewrites every PNG above from
`icon_full.jpg` and the font (deterministic, about 20 s).

## Screenshots (local only)

`res/screenshots/` holds 1920 x 1080 gameplay and launcher screenshots for
store pages. They show the original games' art, so they are **git-ignored and
never committed**. Make them again with the commands in
[`screenshots/HOWTO.md`](screenshots/HOWTO.md) and your own disc images in the
root `original_isos/`.
