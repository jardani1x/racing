# Copyright RacingSim. All Rights Reserved.
#
# TRACK-002: author Content/Tracks/Prototype/Maps/L_Meridian_Graybox.umap.
#
# ===========================================================================
# Why this file exists at all
# ===========================================================================
#
# `.claude/rules/content-safety.md`: "Never edit .uasset or .umap bytes with text/file
# tools" and "Use idempotent source-controlled editor scripts/manifests for repeatable
# changes". A .umap is an opaque binary that cannot be reviewed, diffed or merged, so the
# REVIEWABLE artifact for this level is this script, not the map. Every authored number
# in the level is here, in text, under review, and re-running this script reproduces the
# map from scratch.
#
# CLAUDE.md permits exactly this: "Python may automate editor/content work, but it is not
# runtime gameplay code." Nothing here is runtime gameplay code -- it sets authored
# properties on one C++ actor and saves a map.
#
# ===========================================================================
# Originality -- read before changing the layout
# ===========================================================================
#
# CLAUDE.md forbids copying a real circuit's geometry, name, signage or venue. The
# centerline below is an invented closed loop laid out from scratch to satisfy
# Docs/03-TrackRaceUI.md's first-circuit brief (3-5 km, meaningful elevation change, a
# start/finish straight, and a mix of low/medium/high-speed corners). It is NOT traced
# from, measured against, or derived from any real venue, map, screenshot or game asset.
# The corner names are invented and the circuit name "Meridian" was chosen by CORE-002
# and TRACK-001 as a deliberately neutral placeholder (TRACK-001 finding L1 removed the
# previous "NorthLoop", which read as a translation of a real venue's name).
#
# Anyone editing these coordinates must keep that property true.
#
# ===========================================================================
# What this level deliberately does NOT contain
# ===========================================================================
#
# No static meshes, no materials, no textures, no external content of any kind -- not
# even /Engine/BasicShapes. That is a deliberate license posture, not an oversight: the
# acceptance criterion is "no license-ledger-requiring external asset", and a level whose
# only asset references are its own actors' native classes cannot acquire one by
# accident. A graybox road surface is a spline-mesh problem and belongs to the track-art
# ticket that owns Docs/13-AssetLicenseLedger.md entries.
#
# The level therefore contains: one ATrackDefinitionActor (the deliverable), one
# directional light and one sky light so the map is not pitch black when a human opens
# it. Nothing else.
#
# ===========================================================================
# How to run it
# ===========================================================================
#
#     pwsh -File Scripts/Content/Author-PrototypeGrayboxLevel.ps1
#
# which invokes UnrealEditor-Cmd with -ExecCmds="py <this file>; Quit" -- the SAME
# editor-not-commandlet mode Docs/Environment.md records for the automation gate, chosen
# because that mode is the one this project has actually proven works headlessly on this
# machine.

import math
import unreal

# ===========================================================================
# Authored constants
# ===========================================================================

MAP_PACKAGE_PATH = "/Game/Tracks/Prototype/Maps/L_Meridian_Graybox"

TRACK_ID = "Track.Prototype.Meridian"

# Bake resolution, cm. 100 cm is ATrackDefinitionActor's default and the value every
# TRACK-001/TRACK-002 accuracy claim is stated against.
CENTERLINE_SAMPLE_SPACING_CM = 100.0

# Tightest corner radius the layout below actually contains, cm.
#
# NOT a decorative default. FTrackCenterline::GetSagittaBoundCm turns the baked
# centerline's MAXIMUM segment length into a placement-error bound using this number, and
# every gate's half-width must exceed that bound. The layout's tightest corner is the
# Meridian Hairpin, whose authored control points sit on a radius of roughly 55 m; 15 m
# is therefore a deliberate UNDER-statement, which makes the derived tolerance larger and
# the gate-width check stricter. Understating is the safe direction here; overstating
# would shrink the tolerance and weaken the check.
MIN_CORNER_RADIUS_CM = 1500.0

# Gate extents, cm. Half-width comfortably exceeds any plausible graybox racing surface
# so a car cannot legally pass beside a gate; half-height is finite so a car launched
# over one reports OutsideExtent rather than a clean crossing.
GATE_HALF_WIDTH_CM = 900.0
GATE_HALF_HEIGHT_CM = 500.0

# Number of ordered checkpoint gates, including the start/finish gate at distance 0.
# Six splits a ~3.4 km lap into ~560 m arcs: short enough that a mid-circuit shortcut
# cannot skip a whole sector unnoticed, long enough to stay far outside the
# one-centerline-segment separation floor FRacingCheckpointGateSet::Build enforces.
NUM_CHECKPOINT_GATES = 6

NUM_SECTORS = 3

# ---------------------------------------------------------------------------
# The centerline.
#
# Local-space control points, METRES, in the direction of travel. Point 0 is the
# start/finish line and the centerline's distance origin (ATrackDefinitionActor pins
# those together by design -- see its header). Z carries the elevation change.
#
# Invented layout, corner names invented:
#
#   P0-P2    Meridian Straight     ~600 m start/finish straight, flat, slight rise
#   P3-P5    Ascent Right          fast right, climbing
#   P6-P8    Crest                 medium right over the high point, then downhill
#   P9-P11   Long Back Straight    high-speed, descending
#   P12-P15  Meridian Hairpin      the slowest corner on the circuit, at the low point
#   P16-P18  Rising Esses          medium-speed direction changes, climbing back
#   P19-P22  Compression           long left onto the straight
# ---------------------------------------------------------------------------
CENTERLINE_POINTS_M = [
    (0.0, 0.0, 0.0),          # P0  start/finish line
    (300.0, 0.0, 0.0),        # P1
    (600.0, 0.0, 2.0),        # P2
    (820.0, 30.0, 6.0),       # P3  Ascent Right entry
    (900.0, 140.0, 10.0),     # P4  apex
    (880.0, 300.0, 12.0),     # P5  circuit high point
    (760.0, 400.0, 10.0),     # P6  Crest
    (600.0, 430.0, 6.0),      # P7
    (400.0, 440.0, 3.0),      # P8  Long Back Straight begins
    (220.0, 470.0, 0.0),      # P9
    (120.0, 560.0, -2.0),     # P10 hairpin approach
    (60.0, 620.0, -4.0),      # P11
    (-40.0, 600.0, -4.0),     # P12 Meridian Hairpin, circuit low point
    (-90.0, 500.0, -2.0),     # P13
    (-70.0, 380.0, 0.0),      # P14
    (-140.0, 260.0, 2.0),     # P15 Rising Esses
    (-260.0, 180.0, 4.0),     # P16
    (-320.0, 60.0, 2.0),      # P17
    (-240.0, -60.0, 0.0),     # P18
    (-100.0, -70.0, 0.0),     # P19 Compression
    (-40.0, -30.0, 0.0),      # P20
]

M_TO_CM = 100.0


def log(message):
    unreal.log("[TRACK-002] {0}".format(message))


def fail(message):
    unreal.log_error("[TRACK-002] FAILED: {0}".format(message))
    raise RuntimeError(message)


def author_centerline(spline):
    """Replace the spline's points with the authored layout. Closed loop, curve points."""
    spline.set_closed_loop(True, False)
    spline.clear_spline_points(False)

    for (x_m, y_m, z_m) in CENTERLINE_POINTS_M:
        location = unreal.Vector(x_m * M_TO_CM, y_m * M_TO_CM, z_m * M_TO_CM)
        spline.add_spline_point(location, unreal.SplineCoordinateSpace.LOCAL, False)

    # Explicit rather than inherited: the point type decides whether this is a smooth
    # circuit or a polygon, and a default that changes in a future engine version would
    # silently change the track's length and therefore every lap time on it.
    for index in range(len(CENTERLINE_POINTS_M)):
        spline.set_spline_point_type(index, unreal.SplinePointType.CURVE, False)

    spline.update_spline()
    return float(spline.get_spline_length())


def make_gate_specs(track_length_cm):
    """Evenly spaced ordered gates; gate 0 pinned to exactly 0, the start/finish line."""
    specs = []
    step_cm = track_length_cm / float(NUM_CHECKPOINT_GATES)

    for index in range(NUM_CHECKPOINT_GATES):
        spec = unreal.RacingCheckpointGateSpec()

        # Zero-padded ids so a log line sorts in gate order as text as well as
        # numerically -- Gate.10 before Gate.2 is how an ordering bug gets misread.
        gate_id = "Gate.StartFinish" if index == 0 else "Gate.{0:02d}".format(index)

        # Exactly 0.0 rather than 0 * step, so no rounding step can move the
        # start/finish gate off the distance origin.
        distance_cm = 0.0 if index == 0 else float(index) * step_cm

        # set_editor_property, not attribute assignment: these UPROPERTYs are
        # BlueprintReadOnly, so the generated Python attributes are read-only. The editor
        # accessor honours EditAnywhere instead, which is the access this script has.
        spec.set_editor_property("gate_id", gate_id)
        spec.set_editor_property("distance_along_cm", distance_cm)
        spec.set_editor_property("half_width_cm", GATE_HALF_WIDTH_CM)
        spec.set_editor_property("half_height_cm", GATE_HALF_HEIGHT_CM)
        spec.set_editor_property("legal_direction", unreal.RacingGateDirection.FORWARD)

        specs.append(spec)

    return specs


def make_sector_starts(track_length_cm):
    """Sector 0 starts at the line; the rest divide the lap evenly."""
    step_cm = track_length_cm / float(NUM_SECTORS)
    return [0.0] + [float(index) * step_cm for index in range(1, NUM_SECTORS)]


def spawn_lights(actor_subsystem):
    """A directional light and a sky light so the map is not black. No asset references."""
    zero = unreal.Vector(0.0, 0.0, 0.0)

    sun = actor_subsystem.spawn_actor_from_class(
        unreal.DirectionalLight, unreal.Vector(0.0, 0.0, 5000.0), unreal.Rotator(-45.0, 0.0, -35.0))
    if sun:
        sun.set_actor_label("Sun_Graybox")

    sky = actor_subsystem.spawn_actor_from_class(
        unreal.SkyLight, unreal.Vector(0.0, 0.0, 5000.0), unreal.Rotator(0.0, 0.0, 0.0))
    if sky:
        sky.set_actor_label("SkyLight_Graybox")

    return sun, sky


def main():
    log("Authoring {0}".format(MAP_PACKAGE_PATH))

    # A blank map, not a template: templates pull in engine content (floor mesh, sky
    # sphere, atmosphere blueprints) and this level's whole license posture is that it
    # references none.
    world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
    if not world:
        fail("EditorLoadingAndSavingUtils.new_blank_map returned no world.")

    actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    if not actor_subsystem:
        fail("EditorActorSubsystem is unavailable.")

    track = actor_subsystem.spawn_actor_from_class(
        unreal.TrackDefinitionActor, unreal.Vector(0.0, 0.0, 0.0), unreal.Rotator(0.0, 0.0, 0.0))
    if not track:
        fail("Could not spawn ATrackDefinitionActor.")
    track.set_actor_label("Track_Meridian_Graybox")

    splines = track.get_components_by_class(unreal.SplineComponent)
    if not splines:
        fail("Spawned ATrackDefinitionActor has no USplineComponent.")
    spline = splines[0]

    track_length_cm = author_centerline(spline)
    log("Centerline spline length: {0:.1f} cm ({1:.3f} km)".format(
        track_length_cm, track_length_cm / 100000.0))

    if track_length_cm < 300000.0 or track_length_cm > 500000.0:
        fail("Centerline is {0:.1f} cm; Docs/03-TrackRaceUI.md asks for 3-5 km "
             "(300000-500000 cm).".format(track_length_cm))

    track.set_editor_property("track_id", TRACK_ID)
    track.set_editor_property("centerline_sample_spacing_cm", CENTERLINE_SAMPLE_SPACING_CM)
    track.set_editor_property("min_corner_radius_cm", MIN_CORNER_RADIUS_CM)
    track.set_editor_property("sector_start_distances_cm", make_sector_starts(track_length_cm))
    track.set_editor_property("checkpoint_gate_specs", make_gate_specs(track_length_cm))

    # Authored gates take precedence over generated ones, but the generator's parameters
    # are still hashed into the content version and still validated, so they must be
    # coherent even on a track that never uses them.
    track.set_editor_property("num_generated_checkpoint_gates", NUM_CHECKPOINT_GATES)
    track.set_editor_property("generated_gate_half_width_cm", GATE_HALF_WIDTH_CM)
    track.set_editor_property("generated_gate_half_height_cm", GATE_HALF_HEIGHT_CM)

    # Re-bake explicitly. set_editor_property already fires PostEditChangeProperty, but
    # relying on that would make the saved map depend on property-set ordering.
    if not track.rebuild_track_data():
        fail("RebuildTrackData() returned false; the track did not bake.")

    num_gates = track.get_num_checkpoint_gates()
    log("Baked gates: {0}".format(num_gates))
    for index in range(num_gates):
        log("  gate {0} at {1:.1f} cm".format(
            index, track.get_checkpoint_gate_distance_cm(index)))

    if num_gates != NUM_CHECKPOINT_GATES:
        fail("Expected {0} gates, baked {1}. The gate set did not build; see the "
             "LogRacingRace output above.".format(NUM_CHECKPOINT_GATES, num_gates))

    log("Sectors: {0}".format(track.get_num_sectors()))
    log("Grid slots: {0}".format(track.get_num_grid_slots()))
    log("Reset samples: {0}".format(track.get_num_reset_samples()))

    spawn_lights(actor_subsystem)

    if not unreal.EditorLoadingAndSavingUtils.save_map(world, MAP_PACKAGE_PATH):
        fail("save_map returned false for {0}".format(MAP_PACKAGE_PATH))

    log("SAVED {0}".format(MAP_PACKAGE_PATH))
    log("AUTHORING_OK")


# THE SCRIPT MUST SHUT THE EDITOR DOWN ITSELF.
#
# Found by running: `-ExecCmds="py <script>; Quit"` does NOT exit. Docs/Environment.md's
# automation invocation looks like a precedent for the trailing `Quit`, but it is not one
# -- there, BOTH halves are parsed as `Automation` subcommands ("RunFilter Smoke" and
# "Quit"), and it is the automation controller that quits. As a bare console command,
# `Quit` is not handled here, so the editor came up, ran the script and then sat in its
# tick loop forever under -unattended, producing no further log output and no exit. The
# run had to be killed by PID.
#
# quit_editor() sets the exit request, which is honoured on the next tick -- after this
# script's frame has unwound -- so it is safe to call from inside the script.
#
# In `finally` rather than at the end of main(), because a script that fails MUST still
# exit: a hung headless editor holding a build mutex is worse than a failed authoring run,
# and this machine is shared with other agents' builds.
try:
    main()
finally:
    unreal.log("[TRACK-002] Requesting editor shutdown.")
    unreal.SystemLibrary.quit_editor()
