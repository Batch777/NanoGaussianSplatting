# NanoGS fork — large-PLY tile streaming (+ UE 5.4 support)

Changes on top of upstream TimChen1383/NanoGaussianSplatting.

## Supported versions
- **Unreal Engine 5.4, 5.5, 5.6, 5.7** — Windows 64-bit, **DirectX 12 + Shader Model 6**.
- Verified building **and rendering** on **5.4** and **5.7**; the same source compiles across
  5.4–5.7 via a version shim (`NanoGSRHICompat.h`). Other OS / RHIs (Vulkan, Mac, Linux) untested.

## Installation
**A) From the Release (prebuilt — fastest, no compiler needed if your 5.4 build matches)**
1. Download `NanoGS-TileStream-UE5.4.zip` from the repo's **Releases**.
2. Copy the `Plugins/NanoGS` folder into your UE 5.4 project's `Plugins/` (create it if missing).
3. Set the project to **DX12 + SM6** (see *Hardware*).
4. Open the project. If the prebuilt binaries don't match your exact 5.4 build, accept the
   *rebuild* prompt (needs Visual Studio 2022 + the MSVC note in *Hardware*).
5. If not auto-enabled: **Edit → Plugins**, search **NanoGS**, enable, restart.

**B) From source (any UE 5.4–5.7)**
1. Copy `Plugins/NanoGS` into your project's `Plugins/`.
2. Enable it: add `{ "Name": "NanoGS", "Enabled": true }` to your `.uproject` `Plugins` array
   (or via Edit → Plugins).
3. Set the project to **DX12 + SM6**.
4. Build — open the project and let it compile, or:
   `Build.bat UnrealEditor Win64 Development -Project=<YourProject>.uproject -plugin=<...>/Plugins/NanoGS/NanoGS.uplugin -waitmutex`
   (use your target engine's `Build.bat`; **not** `RunUAT BuildPlugin`).

## Quick start — Tile & Stream UI (manual walkthrough)
1. Enable the **NanoGS** plugin in your project and make sure the project uses **DX12 / SM6**
   (`Config/DefaultEngine.ini`: `DefaultGraphicsRHI=DefaultGraphicsRHI_DX12`,
   `+D3D12TargetedShaderFormats=PCD3D_SM6`). Build the plugin once.
2. In the editor: **Tools → "NanoGS Tile & Stream..."**.
3. **Source PLY**: click `Browse...` and pick your `.ply`.
4. ☑ **Split into tiles** and set **Tile count** (default **48**; more = smaller tiles =
   finer streaming + lower per-tile VRAM). Leave ☑ **Build Nanite LOD** and ☑ **Create
   streaming level** checked.
5. Click **Generate**. It streams-slices the PLY, imports each tile to `/Game/NanoGSTiles`,
   builds Nanite, and creates a streaming level at `/Game/Maps/NanoGSStream`. (Big PLYs take
   a few minutes; a progress dialog shows the stages.)
6. Open **`/Game/Maps/NanoGSStream`** and fly in: tiles load within `LoadRadius`, unload
   beyond `UnloadRadius` (VRAM follows the camera). Select the **TileStreamer** actor to tune
   `Load/Unload Radius`, `Update Every N Frames`, `Async Load`.
- **No split**: leave the checkbox off → the whole PLY imports as a single asset (+ optional Nanite), no streamer.
- **Console equivalent**: `nanogs.GenerateTiles <plypath> [tileCount=48] [split=1]`.

## Hardware / GPU requirements
- **GPU**: requires **DX12 + Shader Model 6** (the renderer uses compute passes incl. a
  9-UAV cluster-culling shader that exceeds the SM5 8-UAV limit). Any **NVIDIA RTX 20-series
  (Turing) or newer**, AMD RDNA, or Intel Arc works. Developed on RTX 4090; an **RTX 3060
  12GB (Ampere)** is plenty.
- **VRAM**: the full ~69M-splat scene is ~9 GB resident if *every* tile is loaded at once.
  On a **12 GB** card keep the **streamer enabled** so only tiles within `LoadRadius` stay
  resident (defaults: Load 300 m / Unload 450 m → a few GB). If you still approach the limit,
  lower `LoadRadius`, raise `Tile count` (smaller tiles), and/or cap working buffers with the
  `gs.MaxRenderBudget <N>` console var (e.g. `8000000`).
- **Project settings** must select DX12 + SM6 (step 1 above). Default SM5 will fail to compile
  the cluster-culling shader.
- **Building on UE 5.4 with a very new MSVC** (≥ 14.4x): UE 5.4's `ConcurrentLinearAllocator.h`
  uses unguarded `__has_feature` → C4668/C4067. Install the MSVC **14.38** toolchain, or add
  `#define __has_feature(x) 0` (non-Clang) near the top of
  `Engine/Source/Runtime/Core/Public/Windows/WindowsPlatformCompilerSetup.h`. (Not needed on 5.5+.)


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
