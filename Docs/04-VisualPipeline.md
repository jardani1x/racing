# Visual and content pipeline

## Art target

The supplied references communicate:

- low trackside and rear-three-quarter cameras;
- realistic motion blur and depth separation;
- strong vehicle clear-coat reflections;
- visible suspension/load transfer and tire contact;
- dense safety barriers, fencing, kerbs, signage, vegetation, and surface variation;
- long elevation views and believable scale;
- race-car interiors, aero, wheel/brake detail, and track grime.

Use these as qualities, not assets to reproduce.

## Rendering stack

Evaluate the following Unreal systems on the reference GPU worker:

- **Nanite** for high-detail static environment geometry and supported vehicle parts where measured beneficial.
- **Lumen** for dynamic global illumination and reflections.
- **Virtual Shadow Maps** for high-resolution dynamic shadows with Nanite/open-world content.
- **TSR** to trade internal resolution for output quality and stable 60 fps.
- **Clear Coat and dual normals** for layered automotive paint and carbon fiber.
- **World Partition** for distance-based streaming and large-world organization.
- **HLOD** for distant non-interactive trackside content.
- **PCG** for controlled vegetation, rocks, clutter, and repeated track infrastructure.

Do not enable every maximum-quality setting at once. Establish benchmark cameras, measure GPU cost, and allocate budgets by visual impact.

## Car material system

Create reusable master materials and instances for:

- metallic flake body paint with a clear-coat layer;
- painted/non-painted carbon fiber with dual-normal response;
- anodized/brushed/bare metal;
- tire rubber with tread/sidewall variation and wear masks;
- brake discs/calipers with heat-ready parameters;
- physically based clear/tinted glass;
- cockpit plastics, fabric, Alcantara-like surfaces, displays, and emissives;
- dirt, rubber pickup, insects, dust, and wetness masks for future use.

Avoid one universal shader with excessive branches. Make bounded material families and profile them.

## Environment material system

Track surfaces require macro and micro variation:

- asphalt aggregate, patches, seams, repairs, rubbered racing line, marbles, skid marks;
- kerb paint with chips, dirt, drainage, and geometry detail;
- grass, gravel, soil, concrete, tire walls, Armco, catch fencing, painted walls;
- decals for original/non-infringing numbering and safety markings;
- distance-aware material complexity and virtual-texture policy where justified.

## Geometry and world building

1. Graybox the full legal route and validate racing flow.
2. Lock the centerline and boundaries before hero art.
3. Build one hero sector at final quality.
4. Validate performance, streaming, collision, camera, and screenshots.
5. Create modular road-edge, drainage, barrier, fence, marshal, gantry, and vegetation kits.
6. Extend around the full circuit using World Partition/OFPA and HLOD.
7. Add art-directed PCG variation; never allow generation to obstruct the legal route or cameras.
8. Run collision and content-stress tests after every sector integration.

For a future licensed real circuit, obtain licensed survey/LiDAR/photogrammetry/reference data and written venue/data permissions before production. Clean, retopologize, UV, material, and create separate collision proxies; raw scans are not production-ready game assets.

## Lighting

Start with one stable dry-day lighting setup:

- physically coherent sun/sky direction and exposure;
- controlled auto-exposure or fixed exposure per camera mode;
- reflection quality tuned around car paint and glass;
- believable contact shadows and cockpit readability;
- calibrated white balance and color pipeline;
- no cinematic grading that destroys driving visibility.

Dynamic time of day and weather are later systems because they multiply material, reflection, visibility, physics, audio, and test states.

## Camera and motion

- Chase camera: subtle speed-based FOV, spring/damping, collision handling, horizon behavior, and suspension-linked motion.
- Cockpit camera: correct eye point, restrained vibration, readable mirrors/instruments, no clipping.
- Trackside/replay cameras: authored lenses and motion; do not use extreme depth of field during gameplay.
- Motion blur must support speed perception without smearing steering inputs or UI.
- Tire rotation, steering, suspension compression, body roll/pitch, aero attitude, and shadows must agree.

## Audio

Create an original or fully licensed layered system for engine RPM/load, intake, exhaust, transmission, tire scrub, curb/road surfaces, wind, impacts, environment, and UI. Do not copy recordings from other games, online videos, or manufacturer media without rights.

## Asset validation

Every imported asset should pass:

- provenance/license ledger;
- naming/folder convention;
- scale, pivot, transform, normals/tangents, UV and material-slot checks;
- collision and physical-material checks;
- Nanite/LOD/HLOD policy;
- texture resolution/format/mip/streaming policy;
- shader complexity and overdraw check;
- reference screenshot check;
- package/cook check.
