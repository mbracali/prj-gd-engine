# Third-party code

gd03 has no third-party code: the disc's audio (VGS and sound-effect samples,
SPU2 PS-ADPCM) and movies (PSS: MPEG-2 video) are decoded by the project's own
decoders, copied from gd-game-01. SDL3 is the only library the program links
(zlib-licensed, fetched by the build scripts).
