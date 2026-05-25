# Headless: load /Game/Main, remove static tile actors, add one GaussianTileStreamer
# (manifest-driven distance streaming), save.
import unreal, json
OUT = r"C:/baidunetdiskdownload/lvyualu-north-0801/ue_streamer.json"
res = {"status": "start"}
json.dump(res, open(OUT, "w"))
try:
    unreal.EditorLoadingAndSavingUtils.load_map("/Game/Main")
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

    # remove the 59 static tile actors (they keep all VRAM resident)
    removed = 0
    for a in eas.get_all_level_actors():
        if isinstance(a, unreal.GaussianSplatActor):
            eas.destroy_actor(a); removed += 1
    # remove any prior streamer
    for a in eas.get_all_level_actors():
        if isinstance(a, unreal.GaussianTileStreamer):
            eas.destroy_actor(a)

    s = eas.spawn_actor_from_class(unreal.GaussianTileStreamer,
                                   unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))
    s.set_actor_label("TileStreamer")
    s.set_editor_property("manifest_path",
                          r"C:/baidunetdiskdownload/lvyualu-north-0801/tiles_pca/manifest.json")
    s.set_editor_property("tile_content_dir", "/Game/TilesPCA")
    s.set_editor_property("load_radius", 18000.0)     # 180 m
    s.set_editor_property("unload_radius", 24000.0)   # 240 m
    s.set_editor_property("update_every_n_frames", 8)
    s.set_editor_property("stream_in_editor", True)
    # rebuild_tile_list() is editor-button only (not Python-exposed); the streamer
    # lazily parses the manifest on its first tick, so just save here.

    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
    res = {"status": "done", "removed_static": removed}
    json.dump(res, open(OUT, "w"), indent=2)
    unreal.log("STREAMER_SETUP removed=%d" % removed)
except Exception as e:
    json.dump({"status": "error", "error": str(e)}, open(OUT, "w"), indent=2)
    unreal.log_error("STREAMER_SETUP_ERR " + str(e))
unreal.SystemLibrary.quit_editor()
