# NanoGS fork — large-PLY tiling + distance streaming

Changes on top of upstream TimChen1383/NanoGaussianSplatting.

## Plugin source changes (`Plugins/NanoGS/`)
- **fp32 splat positions (stripe/moiré fix).** Positions packed full-float (28-byte stride)
  instead of fp16, fixing world-axis banding at large coordinates.
  Files: `Public/GaussianDataTypes.h` (PackedSplatStride=28, PackSplatToUint4),
  `Private/GaussianSplatRenderData.cpp` (i*7 stride), `Shaders/Private/GaussianSplatting.ush`
  (`UnpackSplat` reads `asfloat(Load3(offset+16))`). Optional: revert to fp16 once tiles are
  centered to reclaim ~800MB VRAM.
- **`UGaussianSplatAsset::ReleaseRenderData()`** — frees the shared per-asset GPU buffers so
  tile streaming can actually reclaim VRAM (the big buffers are per-asset, not per-proxy).
  Files: `Public/GaussianSplatAsset.h`, `Private/GaussianSplatAsset.cpp`.
- **`AGaussianTileStreamer`** — manifest-driven, distance-based tile load/unload with async
  background packing (no load hitch) and a one-load-per-pass throttle. Loads tiles within
  `LoadRadius`, destroys + releases beyond `UnloadRadius`. Works in editor viewport and PIE.
  Files: `Public/GaussianTileStreamer.h`, `Private/GaussianTileStreamer.cpp`.
- **Build.cs**: added `Json`, `JsonUtilities`, and editor-only `UnrealEd` (viewport camera).

Build (NOT RunUAT): `Build.bat UnrealEditor Win64 Development -Project=<proj> -plugin=<uplugin> -waitmutex`.
Close the editor before linking (LNK1104 if the DLL is locked).

## Pipeline tools (`tools/`)
- `tile_slicer_pca.py <src.ply> <outdir> <tile_m>` — PCA up-axis, tile in ground plane,
  centre each tile, write `manifest.json` (per-tile `offset` = PLY-xyz centroid).
- `gen_calib.py` + `ue_calib.py` — 4-point calibration to (re)derive the PLY→UE transform.
  Result used here: UE world loc = `(100*oy, 100*ox, 100*oz)` cm + Rotator(Pitch=90).
- `ue_import_pca.py` — headless import tiles → `/Game/TilesPCA`.
- `ue_enable_nanite.py` — build Nanite cluster LOD on every imported tile.
- `ue_setup_streamer.py` / `ue_move_to_level.py` — place + configure the streamer.
- `ue_place_headless.py` — static (non-streamed) placement, for debugging.
NOTE: the `ue_*.py` have hard-coded absolute paths — edit them for your machine.

## UE 5.4 migration notes
- RHI: `RHICmdList.CreateBuffer(FRHIBufferCreateDesc)` + `FRHIViewDesc::CreateBufferSRV()` are
  5.5+ style; on 5.4 use `RHICreateStructuredBuffer/RHICreateVertexBuffer` +
  `RHICreateShaderResourceView` (in `GaussianSplatRenderData.cpp`, `GaussianSplatSceneProxy.cpp`).
- The streamer + ReleaseRenderData + fp32 packing are version-agnostic.
