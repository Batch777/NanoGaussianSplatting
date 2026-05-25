"""
PCA-based tile slicer for large 3DGS PLY -> streamable centered tiles.

Pipeline (step 1 of the auto-tile/stream feature):
  1. PCA over splat centers: smallest-variance principal axis = ground-plane normal = "up".
  2. Tile the cloud in the ground plane (the two large-variance axes), grid = TILE meters.
  3. Each tile: subtract its own centroid (splats centered near origin -> small coords ->
     fp16-safe + distance-streamable), record the centroid as the tile's WorldOffset.
  4. Emit centered tiles (standard PLY, same fields) + manifest.json:
       { up_axis (PLY space), ground_basis, tile_size, tiles:[{file,offset,count}] }

The actor placement step uses offset + up_axis to position/orient each tile actor so the
full scene reconstructs upright, while each tile actor sits at its own world location
(required for World Partition / distance streaming).
"""
import numpy as np
import re
import json
import os
import sys

SRC = sys.argv[1] if len(sys.argv) > 1 else "lvyualu-north-0801.ply"
OUTDIR = sys.argv[2] if len(sys.argv) > 2 else "tiles_pca"
TILE = float(sys.argv[3]) if len(sys.argv) > 3 else 50.0
TRIM = 0.1

NAMES = ['x', 'y', 'z', 'nx', 'ny', 'nz', 'f0', 'f1', 'f2', 'op',
         's0', 's1', 's2', 'r0', 'r1', 'r2', 'r3']
DT = np.dtype([(n, '<f4') for n in NAMES])

print(f"Loading {SRC} ...")
hdr = open(SRC, "rb").read(4096)
he = hdr.find(b"end_header\n") + len(b"end_header\n")
header = hdr[:he]
cnt = int(re.search(rb"element vertex (\d+)", header).group(1))
a = np.fromfile(open(SRC, "rb"), dtype=DT, count=cnt, offset=he)
xyz = np.stack([a['x'], a['y'], a['z']], 1).astype(np.float64)
print(f"  {cnt:,} splats")

# --- PCA for up-axis ---
rng = np.random.default_rng(0)
sub = xyz[rng.choice(cnt, size=min(400000, cnt), replace=False)]
mean = sub.mean(0)
C = np.cov((sub - mean).T)
evals, evecs = np.linalg.eigh(C)            # ascending eigenvalues
up = evecs[:, 0]                             # smallest variance = ground normal
g0 = evecs[:, 2]                             # largest variance = ground axis 1
g1 = evecs[:, 1]                             # 2nd = ground axis 2
if up[2] < 0:                                # prefer up roughly along +PLY.z
    up = -up
print(f"PCA eigenvalues (var): {evals}")
print(f"up-axis (PLY): [{up[0]:.3f} {up[1]:.3f} {up[2]:.3f}]  (1=axis-aligned -> ground is XY)")

# --- tile in ground plane ---
center = xyz.mean(0)
u = (xyz - center) @ g0
v = (xyz - center) @ g1
ulo, uhi = np.percentile(u, TRIM), np.percentile(u, 100 - TRIM)
vlo, vhi = np.percentile(v, TRIM), np.percentile(v, 100 - TRIM)
nu = max(1, int(np.ceil((uhi - ulo) / TILE)))
nv = max(1, int(np.ceil((vhi - vlo) / TILE)))
ti = np.clip(np.floor((u - ulo) / TILE).astype(int), 0, nu - 1)
tj = np.clip(np.floor((v - vlo) / TILE).astype(int), 0, nv - 1)
lin = ti * nv + tj
order = np.argsort(lin, kind="stable")
lin_s = lin[order]
uniq, first = np.unique(lin_s, return_index=True)
bounds = np.append(first, len(order))
print(f"Ground grid: {nu} x {nv} = {nu*nv}, non-empty: {len(uniq)}, tile={TILE}m")

os.makedirs(OUTDIR, exist_ok=True)
tiles = []
for k in range(len(uniq)):
    idx = order[bounds[k]:bounds[k + 1]]
    if len(idx) < 50:
        continue
    sub_a = a[idx].copy()
    cx, cy, cz = float(sub_a['x'].mean()), float(sub_a['y'].mean()), float(sub_a['z'].mean())
    sub_a['x'] -= cx; sub_a['y'] -= cy; sub_a['z'] -= cz
    tid = int(uniq[k]); tx, ty = divmod(tid, nv)
    name = f"tile_{tx}_{ty}.ply"
    h = re.sub(rb"element vertex \d+", f"element vertex {len(idx)}".encode(), header)
    with open(os.path.join(OUTDIR, name), "wb") as f:
        f.write(h); sub_a.tofile(f)
    tiles.append({"file": name, "offset": [cx, cy, cz], "count": int(len(idx))})

manifest = {
    "up_axis_ply": [float(up[0]), float(up[1]), float(up[2])],
    "ground_basis_ply": [[float(g0[0]), float(g0[1]), float(g0[2])],
                          [float(g1[0]), float(g1[1]), float(g1[2])]],
    "tile_size": TILE, "num_tiles": len(tiles), "total": int(cnt),
    "tiles": tiles,
}
json.dump(manifest, open(os.path.join(OUTDIR, "manifest.json"), "w"), indent=2)
print(f"Wrote {len(tiles)} centered tiles + manifest.json to {OUTDIR}")
print(f"  max |offset| extents: {max(abs(t['offset'][i]) for t in tiles for i in range(3)):.1f}m")
