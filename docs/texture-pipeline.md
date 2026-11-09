# AI-enhanced textures

`NetTarget/ObjFac1.textures` holds a true-colour, higher-resolution version of each of the game's larger textures. The game loads it
beside `ObjFac1.dat`; without it (or with `HOVERNET_NO_AI_TEXTURES=1`) every texture is built from the 8-bit palette as before.

## Making the pack (offline, once)

1. `HoverNetTextureExport NetTarget/ObjFac1.dat <dir>` writes `<id>.ppm` and `index.tsv`.
2. `python3 -I tools/texture-ai/enhance_all.py <dir> <realesr-animevideov3.pth> <out>` runs each texture of 64 px or more (power-of-two
   sizes) through SRVGGNetCompact (numpy, no torch; the weights are loaded with a restricted unpickler). Textures are wrap-padded so
   they stay seamless. Output is 4x (128 px -> 512 px). Smaller textures are skipped; the game improves them itself.
3. `python3 -I tools/texture-ai/build_pack.py <out> NetTarget/ObjFac1.textures` packs the PNGs (`HNTXPK1\n`, u32 count, then id, w, h,
   PNG size, PNG per image).

## In the game

`MR_ResBitmap::SetEnhancedImage` gives a texture its image; the colour grade (`ColourGrade`, shared with the palette path) and the
detail pass are applied, and mip levels are built by gamma-correct downsampling at the image's scale. PNGs are decoded with stb_image
v2.30 (MIT / public domain; `THIRD_PARTY_NOTICES.md`). The rasteriser takes the scale per texture.

## Checks

`ctest -R HoverNetTexturePack` (from the repository root): each image belongs to a texture, is a power-of-two multiple of its size,
stays within a mean 22 levels of the original, tiles no worse than the original does (two textures have an edge drawn into the
original), is the scale the texture reports, and `HOVERNET_NO_AI_TEXTURES` switches it off.

## Costs and what was not checked

The pack is 14.2 MB; the true-colour copies take about 78 MB of memory when all are built. The web build does not ship the pack.
Verified on Linux only; Windows installers include it (`skipifsourcedoesntexist`) but the Windows CI copy steps were not changed and
the Windows build was not run. The model is the anime-video Real-ESRGAN variant, chosen by eye over the general model; check its
licence in `THIRD_PARTY_NOTICES.md` before redistributing the pack. In a real-client frame the difference is subtle on flat surfaces.
