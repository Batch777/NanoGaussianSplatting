# NanoGS fork — large-PLY tile streaming (+ UE 5.4 support)

Changes on top of upstream TimChen1383/NanoGaussianSplatting. Builds & runs on **UE 5.4–5.7**.

## Plugin source changes (`Plugins/NanoGS/`)
- **fp32 splat positions (stripe/moiré fix).** Positions packed full-float (28-byte stride)
  instead of fp16, fixing world-axis banding at large coordinates.
- **`UGaussianSplatAsset::ReleaseRenderData()`** — frees the shared per-asset GPU buffers so
  tile streaming can actually reclaim VRAM (the big buffers are per-asset, not per-proxy).
- **`AGaussianTileStreamer`** — manifest-driven, distance-based tile load/unload with async
  background packing (no load hitch) and one-load-per-pass throttle. Loads tiles within
  `LoadRadius`, destroys + releases beyond `UnloadRadius`. Works in editor viewport and PIE.
- **CalcDistances**: linear view-space depth sort key. **CalcViewData**: Mip-Splatting AA
  opacity compensation. **Build.cs**: Json/JsonUtilities + editor-only UnrealEd.

## UE 5.4 support (verified: compiles + renders)
The code targets the 5.5+ RHI API; `NanoGSRHICompat.h` makes it build on 5.4:
- Back-ports `FRHIBufferCreateDesc` + routes all buffer creation through `GSCreateBuffer()`
  (forwards to `RHICmdList.CreateBuffer(Desc)` on 5.5+). The 5.4 path **OR-s in
  `ShaderResource`** so D3D12 doesn't set `DENY_SHADER_RESOURCE` on UAV buffers that are
  also read as SRVs (otherwise: render-thread assert at D3D12CommandList.cpp:573).
- Explicit includes added for 5.4: `DataDrivenShaderPlatformInfo.h` (IsFeatureLevelSupported),
  `MaterialDomain.h` (MD_Surface).

To use the fork on UE 5.4 you ALSO need (environment, not plugin code):
1. **Project must use SM6 / DX12.** A default project is SM5 (8-UAV limit) and the cluster-
   culling shader uses 9 UAVs → "Number of UAVs exceeded limit" fatal. In `DefaultEngine.ini`:
   `DefaultGraphicsRHI=DefaultGraphicsRHI_DX12` and `+D3D12TargetedShaderFormats=PCD3D_SM6`.
2. **If your MSVC is newer than UE 5.4 supports** (e.g. 14.4x): UE 5.4's
   `ConcurrentLinearAllocator.h` uses unguarded `__has_feature` → C4668/C4067 errors. Either
   install the MSVC 14.38 toolchain, or add `#define __has_feature(x) 0` (non-Clang) near the
   top of `Engine/Source/Runtime/Core/Public/Windows/WindowsPlatformCompilerSetup.h`.

Build (NOT RunUAT): `Build.bat UnrealEditor Win64 Development -Project=<proj> -plugin=<uplugin> -waitmutex`.
Close the editor before linking (LNK1104 if the DLL is locked).

## Pipeline tools (`StreamingTools/`)
`tile_slicer_pca.py` (PCA up-axis, ground-plane tiling, per-tile centering + manifest),
`gen_calib.py`/`ue_calib.py` (PLY→UE transform calibration: world loc = (100·oy,100·ox,100·oz)cm + Pitch90),
`ue_import_pca.py`, `ue_enable_nanite.py`, `ue_setup_streamer.py`/`ue_move_to_level.py`.
The `ue_*.py` have hard-coded absolute paths — edit for your machine. Note 5.4-saved assets
don't open in 5.7 and vice-versa, so re-import tiles per engine version.
