# Headless: load /Game/Main, clear GS actors, place 59 PCA tiles (calibrated transform), save.
import unreal, json
OUT = r"C:/baidunetdiskdownload/lvyualu-north-0801/ue_place_hl.json"
MAN = r"C:/baidunetdiskdownload/lvyualu-north-0801/tiles_pca/manifest.json"
res = {"status": "start", "placed": 0}
json.dump(res, open(OUT, "w"))
try:
    unreal.EditorLoadingAndSavingUtils.load_map("/Game/Main")
    man = json.load(open(MAN))
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    for a in eas.get_all_level_actors():
        if isinstance(a, unreal.GaussianSplatActor):
            eas.destroy_actor(a)
    up = unreal.Rotator(pitch=90.0, yaw=0.0, roll=0.0)
    vmin = [1e18, 1e18, 1e18]; vmax = [-1e18, -1e18, -1e18]
    for t in man["tiles"]:
        name = t["file"].replace(".ply", "")
        asset = unreal.load_asset("/Game/TilesPCA/" + name)
        if not asset:
            continue
        ox, oy, oz = t["offset"]
        loc = unreal.Vector(100.0 * oy, 100.0 * ox, 100.0 * oz)   # calibrated
        a = eas.spawn_actor_from_object(asset, loc, up)
        if a:
            a.set_actor_label(name); res["placed"] += 1
            o, e = a.get_actor_bounds(False)
            for i, lo, hi in [(0, o.x-e.x, o.x+e.x), (1, o.y-e.y, o.y+e.y), (2, o.z-e.z, o.z+e.z)]:
                vmin[i] = min(vmin[i], lo); vmax[i] = max(vmax[i], hi)
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
    res["status"] = "done"
    res["size_m"] = [round((vmax[i]-vmin[i])/100.0, 1) for i in range(3)]
    json.dump(res, open(OUT, "w"), indent=2)
    unreal.log("PLACE_HL placed=%d size_m=%s" % (res["placed"], res["size_m"]))
except Exception as e:
    json.dump({"status": "error", "error": str(e)}, open(OUT, "w"), indent=2)
unreal.SystemLibrary.quit_editor()
