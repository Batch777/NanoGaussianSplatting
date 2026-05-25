# Headless import of centered PCA tiles -> /Game/TilesPCA
import unreal, os, json
TILES_DIR = r"C:/baidunetdiskdownload/lvyualu-north-0801/tiles_pca"
DEST = "/Game/TilesPCA"
OUT = r"C:/baidunetdiskdownload/lvyualu-north-0801/ue_import_pca.json"
plys = sorted(f for f in os.listdir(TILES_DIR) if f.lower().endswith(".ply"))
res = {"status": "start", "ok": 0, "total": len(plys)}
open(OUT, "w").write(json.dumps(res))
atools = unreal.AssetToolsHelpers.get_asset_tools()
factory = unreal.GaussianSplatAssetFactory()
for fn in plys:
    t = unreal.AssetImportTask()
    t.set_editor_property("filename", os.path.join(TILES_DIR, fn).replace("\\", "/"))
    t.set_editor_property("destination_path", DEST)
    t.set_editor_property("destination_name", os.path.splitext(fn)[0])
    t.set_editor_property("factory", factory)
    t.set_editor_property("automated", True)
    t.set_editor_property("replace_existing", True)
    t.set_editor_property("save", True)
    atools.import_asset_tasks([t])
    if t.get_editor_property("imported_object_paths"):
        res["ok"] += 1
    open(OUT, "w").write(json.dumps(res))
res["status"] = "done"
open(OUT, "w").write(json.dumps(res))
unreal.log("IMPORT_PCA_DONE %d/%d" % (res["ok"], res["total"]))
