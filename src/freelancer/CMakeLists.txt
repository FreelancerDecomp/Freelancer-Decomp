# See cmake/Freelancer.cmake. The source list is the link order, and with it the order of
# static initialisation.

# PlayerDB creates Restart.fl from NewPlayer.fl beside the executable: it is tracked in bin\ and
# copied into the other output directories by copy_dev_ini.py (dacom's fl_copy_dev_ini).

fl_add_module(freelancer EXE RETAIL PCH EXPORTS
  SOURCES
    Client/client_state.cpp
    Client/voice_profiles.cpp
    Client/sound_config.cpp
    Client/nn_preferences.cpp
    Client/wincontrol_base.cpp
    Client/win3d.cpp
    Client/text_win.cpp
    Client/type_select.cpp
    Client/page_options.cpp
    Client/page_account_select.cpp
    Client/win_object.cpp
    Client/list_box.cpp
    Client/rich_text_win.cpp
    Client/ui_page_desc.cpp
    Client/ui_page.cpp
    Client/ui_rich_text_win.cpp
    Client/win_factory.cpp
    Client/ui_static_text.cpp
    Client/ui_list_box.cpp
    Client/ui_scroll.cpp
    Client/nn_button_control.cpp
    Client/nn_static_control.cpp
    Client/nn_montage_draw.cpp
    Client/nn_text_control.cpp
    Client/wire_controls.cpp
    Client/nn_prompt.cpp
    Client/nn_modal.cpp
    Client/nn_main_button.cpp
    Client/nav_bar.cpp
    Client/infocard_map.cpp
    Client/knowledge_map.cpp
    Client/nn_messaging.cpp
    Client/nn_mission_message.cpp
    Client/character_info.cpp
    Client/nn_exit.cpp
    Client/nn_help.cpp
    Client/nn_player_info.cpp
    Client/nn_offer_dialog.cpp
    Client/nn_pause.cpp
    Client/nn_confirm.cpp
    Client/nn_load_save.cpp
    Client/nn_network_operation.cpp
    Client/nn_error.cpp
    Client/nn_blocking_dialog.cpp
    Client/wincontrol_children.cpp
    Client/nn_ui_pulse.cpp
    Client/nn_tooltip.cpp
    Client/nn_rollover.cpp
    Client/nn_group_control.cpp
    Client/menu_mp_session.cpp
    Client/font_string.cpp
    Client/font_imp.cpp
    Client/font_atlas.cpp
    Client/menu_enter_state.cpp
    Client/ui_text_msg_button.cpp
    Client/ui_text_edit.cpp
    Client/ui_win_deform.cpp
    Client/ui_ship_view_control.cpp
    Client/ui_win_object.cpp
    Client/ui_table.cpp
    Client/ui_fast_table.cpp
    Client/mission_briefing.cpp
    Client/client_frame_tick.cpp
    Client/client_tick_profile.cpp
    Client/connect_handshake.cpp
    Client/connect_lists.cpp
    Client/login_mgr.cpp
    Client/cockpit_load.cpp
    Client/player_behavior.cpp
    Client/player_spawn.cpp
    Client/client_objdb.cpp
    Client/client_gameobj.cpp
    Client/client_debris.cpp
    Client/client_explosion.cpp
    Client/client_land_action.cpp
    Client/character_select_equip.cpp
    Client/client_equip_fx.cpp
    Client/client_fuse.cpp
    Client/tradelane_fx.cpp
    Client/effect_base.cpp
    Client/effect_vertex_batch.cpp
    Client/beam_effect.cpp
    Client/cli_projectile_fx.cpp
    Client/cli_tractor_beam.cpp
    Client/fl_fx_mesh.cpp
    Client/particle_effect.cpp
    Client/alchemy_float_forward.cpp
    Client/effect_type_info.cpp
    Client/effect_type_list.cpp
    Client/ship_camera_params.cpp
    Client/space_camera.cpp
    Client/space_camera_third.cpp
    Client/space_camera_game.cpp
    Client/space_camera_rear.cpp
    Client/space_camera_turret.cpp
    Client/space_camera_first.cpp
    Client/space_camera_script.cpp
    Client/space_letterbox.cpp
    Client/fade_to_black.cpp
    Client/space_camera_static.cpp
    Client/space_camera_docking.cpp
    Client/space_camera_death.cpp
    Client/win3d_immediate.cpp
    Client/cli_asteroid.cpp
    Client/cli_asteroid_dynamic.cpp
    Client/cli_asteroid_med.cpp
    Client/asteroid_billboard.cpp
    Client/cli_nebula.cpp
    Client/texture_panels.cpp
    Client/star_sphere.cpp
    Client/star_sys.cpp
    Client/part_cache.cpp
    Client/space_dust.cpp
    Client/space_scene_draw.cpp
    Client/space_scene_obj_draw.cpp
    Client/render_ex.cpp
    Client/nebula_volume.cpp
    Client/space_scene_load.cpp
    Client/space_solar_world.cpp
    Client/scene_render_registry.cpp
    Client/space_state.cpp
    Client/win3d_render.cpp
    Client/win3d_engine_submit.cpp
    Client/win3d_renderables.cpp
    Client/intro_connect.cpp
    Client/image.cpp
    Client/loading_screen.cpp
    Client/pixel_format.cpp
    Client/menu_universe_base.cpp
    Client/room_control.cpp
    Client/room_control_hotspot.cpp
    Client/anim_script_set.cpp
    Client/flash_light.cpp
    Client/gameflow.cpp
    Client/base_walk.cpp
    Client/gf_hotspot.cpp
    Client/object_prop.cpp
    Client/room_camera.cpp
    Client/gf_script.cpp
    Client/gf_script_object.cpp
    Client/gf_thorn_component.cpp
    Client/gf_thorn_compound.cpp
    Client/gf_thorn_deformable.cpp
    Client/gf_character_object.cpp
    Client/gf_script_spawn.cpp
    Client/gf_thorn_camera.cpp
    Client/gf_thorn_light.cpp
    Client/gf_thorn_marker.cpp
    Client/gf_thorn_psys.cpp
    Client/gf_thorn_scene.cpp
    Client/gf_thorn_sound.cpp
    Client/gf_object_ship.cpp
    Client/lights_camera.cpp
    Client/menu_sound_arch.cpp
    Client/engine_light.cpp
    Client/graph_library.cpp
    Client/misc_util.cpp
    Client/texture_shapes.cpp
    Client/hud_element_list.cpp
    Client/hud_command_dispatch.cpp
    Client/hud_comm.cpp
    Client/hud_targetable_objects.cpp
    Client/button_mover.cpp
    Client/hud_target.cpp
    Client/hud_target_panel.cpp
    Client/hud_status_core.cpp
    Client/hud_weapon_groups.cpp
    Client/hud_cruise_progress.cpp
    Client/hud_contact_list.cpp
    Client/hud_damage_indicator.cpp
    Client/hud_maneuvers.cpp
    Client/hud_waypoint.cpp
    Client/hud_widescreen.cpp
    Client/jump_effect.cpp
    Client/best_path.cpp
    Client/client_cliobj.cpp
    # Static-init order (retail XC table): the material cache (client_cliobj.cpp, slot 0x0060f0bc)
    # is built before the VisEffect map (vis_effect_arch.cpp, slot 0x0060f19c), so ~FVisEffectArch
    # (FUN_0042edb0) still finds it at exit. MSVC runs .CRT$XCU in link order, which is this
    # order: keep vis_effect_arch.cpp after client_cliobj.cpp.
    Client/vis_effect_arch.cpp
    Client/client_mesh_load.cpp
    Client/engine_character.cpp
    Client/hud_button.cpp
    Client/input_manager.cpp
    Client/client_ime_focus.cpp
    Client/fl_ime_input_box.cpp
    Client/chat_text_input.cpp
    Client/chat_control.cpp
    Client/perf_options.cpp
    Client/menu_ui_draw.cpp
    Client/menu_ui_widgets.cpp
    Client/nn_repair.cpp
    Client/nn_story_star.cpp
    Client/nn_ship_trader.cpp
    Client/nn_trade.cpp
    Client/cargo_bar_control.cpp
    Client/equip_visual_set.cpp
    Client/scroll_control.cpp
    Client/trade_equip_list.cpp
    Client/ship_view_control.cpp
    Client/nn_inventory.cpp
    Client/nn_dealer.cpp
    Client/commodity_by_base.cpp
    Client/client_base_market.cpp
    Client/swapped_material.cpp
    Client/nn_mission_vendor.cpp
    Client/nn_news.cpp
    Client/nn_navmap.cpp
    Client/nn_navmap_panels.cpp
    Client/nn_navmap_update.cpp
    Client/nn_navmap_build.cpp
    Client/nn_navmap_view.cpp
    Client/nn_navmap_draw.cpp
    Client/nn_navmap_input.cpp
    Client/mission_created_solars.cpp
    Client/nn_scanner.cpp
    Client/nn_text_scratch.cpp
    Client/main_command_handler.cpp
    Client/menu_keymap.cpp
    Client/screen_quad_state.cpp
    Client/connect_flow.cpp
    Client/client_exports.cpp
    Client/client_iclient.cpp
    Client/client_iclient_space.cpp
    Client/main.cpp
    Client/window.cpp
    Client/message_pump.cpp
    Client/engine_init.cpp
    Client/menu_ui_init.cpp
    Client/dataconfig_progress.cpp
    Client/menu_fonts_load.cpp
    Client/ids_strings.cpp
    Client/ids_rdl.cpp
    Client/dealer_equip_stats.cpp
    Client/chat_init.cpp
    Client/game_options.cpp
    Client/gate_tunnel.cpp
    Client/client_timing.cpp
    Client/server_connect.cpp
    Client/server_filter.cpp
    Client/reg_key_stack.cpp
    Client/page_server_select.cpp
    Client/server_filter_dialog.cpp
    Client/intro_page_util.cpp
    Client/page_character_create.cpp
    Client/page_character_select.cpp
    Client/page_character_select_single.cpp
    Client/startup_config.cpp
    resources/freelancer.rc
)

# FREELANCER_EXPORTS selects the declarations freelancer.exe exports itself (retail FreeLancer.exe
# carries a 69-name export table, no .def): see the export macros in src/include/rpc_iclient.h and
# src/include/common_exports.h.
fl_defines(freelancer FREELANCER_EXPORTS)
fl_rc(freelancer INCLUDES resources)

# The port-only switches (cmake/Freelancer.cmake): only the client reads them.
if(FreelancerBetaLabel)
  fl_defines(freelancer FREELANCER_BETA_LABEL=1)
endif()
if(FreelancerWidescreenSupport)
  fl_defines(freelancer FREELANCER_WIDESCREEN_SUPPORT)
endif()
if(FreelancerRp9)
  fl_defines(freelancer FREELANCER_RP9)
endif()
if(FreelancerWalkMode)
  fl_defines(freelancer FREELANCER_WALK_MODE)
endif()

fl_item(Client/alchemy_float_forward.cpp
  BasicRuntimeChecks Default Optimization MaxSpeed DebugInformationFormat ProgramDatabase
  SupportJustMyCode false EnableEnhancedInstructionSet StreamingSIMDExtensions2)

if(FL_WIN32)
  set(_libdirs ../bin ${PROJECT_SOURCE_DIR}/bin)
else()
  set(_libdirs)
endif()
fl_link(freelancer
  LIBS dacom.lib DALib.lib Common.lib imeui.lib ole32.lib gdi32.lib user32.lib advapi32.lib
       shell32.lib winmm.lib
  REFERENCES dacom DALib Common imeui
  LIBDIRS ${_libdirs})

# DebugASan: the AddressSanitizer runtime DLLs next to the exe.
fl_copy_asan_runtime(freelancer)
