# 4-point calibration PLY using the REAL tile property names (importer requires them).
# Points (PLY meters): origin, +X(10m), +Y(20m), +Z(30m) -> distinct lengths per axis.
import numpy as np
NAMES = ['x','y','z','nx','ny','nz','f_dc_0','f_dc_1','f_dc_2','opacity',
         'scale_0','scale_1','scale_2','rot_0','rot_1','rot_2','rot_3']
DT = np.dtype([(n,'<f4') for n in NAMES])
pts = [(0,0,0),(10,0,0),(0,20,0),(0,0,30)]
a = np.zeros(len(pts), dtype=DT)
for i,(x,y,z) in enumerate(pts):
    a['x'][i],a['y'][i],a['z'][i] = x,y,z
    a['opacity'][i] = 8.0
    a['scale_0'][i]=a['scale_1'][i]=a['scale_2'][i] = -2.0
    a['rot_0'][i] = 1.0
    a['f_dc_0'][i]=a['f_dc_1'][i]=a['f_dc_2'][i] = 1.0
hdr = b"ply\nformat binary_little_endian 1.0\nelement vertex %d\n" % len(pts)
for n in NAMES:
    hdr += b"property float " + n.encode() + b"\n"
hdr += b"end_header\n"
with open("calib.ply","wb") as f:
    f.write(hdr); a.tofile(f)
print("wrote calib.ply", len(pts), "points")
