# Move tile streaming into its own level for clean management.
#  1) strip streamer/tile actors out of /Game/Main and save it clean
#  2) create /Game/Maps/TileStream with one fully-configured GaussianTileStreamer
# Run in the open editor:  py C:/baidunetdiskdownload/lvyualu-north-0801/ue_move_to_level.py
import unreal

les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

# 1) clean Main
les.load_level("/Game/Main")
n = 0
for a in eas.get_all_level_actors():
    if isinstance(a, (unreal.GaussianTileStreamer, unreal.GaussianSplatActor)):
        eas.destroy_actor(a); n += 1
les.save_current_level()
unreal.log("MOVE: removed %d streaming actors from Main" % n)

# 2) dedicated streaming level
les.new_level("/Game/Maps/TileStream")
s = eas.spawn_actor_from_class(unreal.GaussianTileStreamer,
                               unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))
s.set_actor_label("TileStreamer")
s.set_editor_property("manifest_path",
                      r"C:/baidunetdiskdownload/lvyualu-north-0801/tiles_pca/manifest.json")
s.set_editor_property("tile_content_dir", "/Game/TilesPCA")
s.set_editor_property("load_radius", 18000.0)          # 180 m
s.set_editor_property("unload_radius", 26000.0)        # 260 m (hysteresis)
s.set_editor_property("update_every_n_frames", 6)
s.set_editor_property("stream_in_editor", True)
s.set_editor_property("async_load", True)
les.save_current_level()
unreal.log("MOVE: created /Game/Maps/TileStream with configured streamer")
print("done -> /Game/Maps/TileStream ; Main cleaned (removed %d)" % n)
