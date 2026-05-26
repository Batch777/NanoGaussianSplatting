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

## Reducing VRAM usage
Levers, biggest impact first:
- **Keep the streamer enabled** (the main lever). Only tiles within `LoadRadius` stay resident;
  far tiles' GPU buffers are released. Lower the TileStreamer's `LoadRadius`/`UnloadRadius` to
  hold fewer tiles at once.
- **More, smaller tiles** — raise **Tile count** when generating (e.g. 48 → 96). Smaller tiles =
  finer streaming granularity = less resident at any moment.
- **`gs.MaxRenderBudget <N>`** — caps the per-frame sort/working buffers to N splats (closer
  tiles get priority), e.g. `gs.MaxRenderBudget 8000000`. Put it in `Config/DefaultEngine.ini`
  under `[ConsoleVariables]` so it applies at startup (the buffers are sized then). `0` = unlimited.
- **Keep "Build Nanite LOD" on** — far tiles render coarse clusters, shrinking the working set.
  `gs.DebugForceLODLevel <n>` forces a coarser level for testing.
- **Measure it**: the full ~69M-splat scene is ~9 GB if every tile loads at once; with default
  streaming only a few GB stay resident. Watch live with `stat RHI` (Structured + ByteAddress
  buffer lines) or `nvidia-smi`.
- *(Advanced)* this fork stores **fp32** positions (28-byte) to fix banding on large coordinates —
  ~+75% on the position buffer vs fp16. Since tiles are centred (small local coords), switching
  the packing back to fp16 would reclaim that; not enabled by default.


---

# Upstream README (TimChen1383/NanoGaussianSplatting)

# Nano Gaussian Splatting

![Image](https://github.com/user-attachments/assets/83ae3ba1-9414-4cb5-8703-19a15abddd21)



- Supported Unreal Engine version: `UE5.6` `UE5.7`
- Download the plugin from release page to use it directly
- The plugin download from repoitory need to be re-build

## Overview
Rendering large scale gaussian splatting files is always a big challenge.
In order to render large scale gaussian splatting files in real-time, we will need to carefully handle VRAM, sorting millions of splats efficiently and drawing only the needed splats for the screen.

The plugin leverages techniques such as Nanite-style LOD Clusters, Screen-Space Error LOD Selection, Splat Compaction, Global Accumulator and GPU Radix Sort.
As a result users can render large scale gaussian splitting scenes efficiently.

## How To Use It
Video Tutorial

[![Watch the video](https://img.youtube.com/vi/5nyo6cfvio0/0.jpg)](https://www.youtube.com/watch?v=5nyo6cfvio0)

- Download the plugin from Releases page
- Place the plugin inside Unreal Engine project's "Plugins" folder
- Press import button to import PLY file. A Gaussian Splat Asset will be created

![Image](https://github.com/user-attachments/assets/2d784b1e-c2a1-4cb3-a891-2e80b84e6c28)

- Drag the Gaussian Splat Asset directly into the level

![Image](https://github.com/user-attachments/assets/52d63cca-e850-478b-a6bb-ed88efce1f97)

- Enable/Disable Nanite through asset action if needed

![Image](https://github.com/user-attachments/assets/5b5d7d44-a142-46ae-bbb6-bcae1856cca3)


## Settings
<img width="828" height="453" alt="Image" src="https://github.com/user-attachments/assets/d7314cbf-b0b9-4748-ad5f-ba87375fbfef" />

| Category | Variable | Description |
| :--- | :--- | :--- |
| Quality | SH Order | sphere harmonics quality |
| Performance | Sort Every Nth Frame | adjust gaussian splats sorting speed |
| Performance | Enable Frustum Culling | enable/disable frustum culling |
| Performance | LOD Error Threshold | LOD cluster switching sensitivity |
| Rendering | Opacity Scale | adjust the opacity of gaussian splats|
| Rendering | Splat Scale | adjust the scale of gaussian splats|


## Debug Console Command
- `gs.ShowClusterBounds 1`: enable Nanite cluster preview (set to 0 to disable preview)
- `gs.DebugForceLODLevel ?`: force render a specific LOD cluster for debugging purpose (can be 1,2,3,4...)
- `gs.MaxRenderBudget ?`: limit the max number of visible splats(after culling) for saving VRAM. By default there is no limitaion(0). Set the max cap to decrease the VRAM usage (ex:3,000,000). The culling will start from the splats which are far from the camera

## Best Practice
![Image](https://github.com/user-attachments/assets/76089944-18a8-4a3d-b6f9-52045a72acb8)

A single big chunk of gaussian splatting file is not conducive to performance optimization. 
The splats outside the camera view can not be culled. 
Also, all the splats will be engaged in the sorting process all the time.

The ideal format is slicing a big chunk of gaussian splatting file into smaller pieces.
For example individual props for cinematic scenes or tiles for geo-spatial data (this repo includes a simple python tile slicer).

## Debug

- Almost transparent gaussian splats currently will generate ghost effect with TSR. Might need to clean up the transparent splats or switch to use FXAA.
<img width="1387" height="746" alt="Transparent" src="https://github.com/user-attachments/assets/493bb345-c68b-4687-85a5-8aa734fbf18f" />

