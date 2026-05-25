# Calibrate PLY->UE axis mapping. Imports calib.ply, places at origin+Pitch90,
# reads the actor bounds. Points are at PLY +X(10m),+Y(20m),+Z(30m) so each UE axis's
# extent (1000/2000/3000 cm) tells which PLY axis maps there, and which bound is 0 tells sign.
# Run:  py C:/baidunetdiskdownload/lvyualu-north-0801/ue_calib.py
import unreal, json

atools = unreal.AssetToolsHelpers.get_asset_tools()
t = unreal.AssetImportTask()
t.set_editor_property("filename", r"C:/baidunetdiskdownload/lvyualu-north-0801/calib.ply")
t.set_editor_property("destination_path", "/Game/CalibTmp")
t.set_editor_property("destination_name", "calib")
t.set_editor_property("factory", unreal.GaussianSplatAssetFactory())
t.set_editor_property("automated", True)
t.set_editor_property("replace_existing", True)
t.set_editor_property("save", True)
atools.import_asset_tasks([t])

asset = unreal.load_asset("/Game/CalibTmp/calib")
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
a = eas.spawn_actor_from_object(asset, unreal.Vector(0, 0, 0),
                                unreal.Rotator(pitch=90.0, yaw=0.0, roll=0.0))
o, e = a.get_actor_bounds(False)
res = {"origin": [o.x, o.y, o.z], "extent": [e.x, e.y, e.z],
       "min": [o.x - e.x, o.y - e.y, o.z - e.z],
       "max": [o.x + e.x, o.y + e.y, o.z + e.z]}
json.dump(res, open(r"C:/baidunetdiskdownload/lvyualu-north-0801/ue_calib.json", "w"), indent=2)
unreal.log("CALIB min=%s max=%s" % (res["min"], res["max"]))
print("CALIB", res)
