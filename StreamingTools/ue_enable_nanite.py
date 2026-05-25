# Enable Nanite (build cluster hierarchy + LOD) on every /Game/Tiles asset, then save.
# Headless:  UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script="<this file>" -NullRHI
import unreal
import json

OUT = r"C:/baidunetdiskdownload/lvyualu-north-0801/ue_nanite.json"
ar = unreal.AssetRegistryHelpers.get_asset_registry()
assets = ar.get_assets_by_path("/Game/TilesPCA", recursive=True)
gs = [a for a in assets if a.get_class().get_name() == "GaussianSplatAsset"]

res = {"status": "starting", "done": 0, "ok": 0, "total": len(gs), "tiles": []}


def w():
    with open(OUT, "w") as f:
        json.dump(res, f, indent=2)


w()
for a in gs:
    pkg = str(a.package_name)
    name = str(a.asset_name)
    obj = unreal.load_asset(pkg)
    try:
        built = bool(obj.build_nanite_cluster_hierarchy())
        clusters = int(obj.get_cluster_count())
        lods = int(obj.get_num_lod_levels())
        nanite = bool(obj.is_nanite_enabled())
        if built:
            unreal.EditorAssetLibrary.save_asset(pkg, only_if_is_dirty=False)
            res["ok"] += 1
        res["tiles"].append({"name": name, "built": built, "clusters": clusters,
                             "lods": lods, "nanite": nanite})
    except Exception as e:
        res["tiles"].append({"name": name, "error": str(e)})
    res["done"] += 1
    w()

res["status"] = "done"
w()
unreal.log("NANITE_DONE %d/%d ok" % (res["ok"], res["total"]))
