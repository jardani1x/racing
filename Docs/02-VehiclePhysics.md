# Vehicle physics plan

## Correction to the original brief

Unreal Engine does not use Unity's `WheelCollider` component. The Phase 1 implementation should use Chaos Vehicles with properly authored wheel geometry/bones, wheel and suspension configuration, vehicle collision, drivetrain configuration, steering, brakes, and tire/friction inputs.

## Phase 1: stable baseline

Build one original prototype with these systems:

1. Correct real-world scale and explicit SI-to-Unreal conversions.
2. Chassis mass, inertia treatment, and center of mass.
3. Four wheel definitions with measured radius/width and correct rotation/steer axes.
4. Suspension travel, spring/damping, anti-roll policy, and collision traces.
5. Engine torque curve, idle/redline, engine braking, clutch policy.
6. Gear ratios, final drive, automatic/manual shift policy, reverse.
7. Differential configuration.
8. Per-axle braking, handbrake, steering ratio/curve, speed sensitivity.
9. Baseline tire friction and combined-slip behavior available through Chaos.
10. Aerodynamic drag and downforce with a documented center of pressure.
11. ABS/TCS as separate, testable controllers; start disabled unless required for stability.
12. Reset/recovery that preserves race validity rules.
13. Telemetry capture at the simulation rate or a documented decimation rate.

Keep the first tune simple and stable. Do not chase final realism before the track, telemetry, tests, and frame-time policy exist.

## Simulation timing

- Gameplay behavior must not be tied to render Tick.
- During the physics spike, choose a fixed/substepped simulation policy that the reference GPU worker can sustain. A 120 Hz candidate is reasonable to test, not a promise.
- Record the actual step, substep count, and fallback behavior.
- Test at different render rates and under streaming load.
- Detect and fail tests on NaN, infinity, tunneling, unstable wheel state, or runaway energy.

## Telemetry schema

At minimum record:

- monotonic timestamp and physics step;
- world position/orientation and linear/angular velocity;
- vehicle speed and longitudinal/lateral acceleration;
- throttle, brake, clutch, steering, gear, RPM;
- per-wheel angular speed, suspension position/velocity, contact state, normal load, longitudinal/lateral slip when available;
- yaw rate and steering angle;
- aero drag/downforce;
- ABS/TCS state;
- surface/material identifier;
- race spline distance and lap validity.

Export deterministic test summaries rather than uncontrolled per-frame logs in normal builds.

## Validation manoeuvres

Automate or record input for:

- stationary idle and no-creep check;
- straight-line launch and shift sequence;
- coast-down from a defined speed;
- full braking from two defined speeds;
- constant-radius skidpad in both directions;
- step steer and lane change;
- curb strike and suspension recovery;
- hill start and downhill braking;
- reverse, spin, off-track, and reset;
- 30-minute lap/soak sequence.

Use envelopes rather than fake precision until source data is authoritative. For a licensed car, replace prototype envelopes with manufacturer-approved specifications and sign-off.

## Phase 2: higher fidelity, only if evidence requires it

Stock Chaos may be sufficient for an accessible sim-racing vertical slice. If telemetry and expert evaluation show that it cannot meet the handling target, add a project-owned C++ layer for:

- load-sensitive combined-slip tire forces;
- tire temperature, pressure, wear, and surface state;
- suspension kinematics and compliance;
- more detailed clutch/differential/driveline behavior;
- aero maps, ride-height sensitivity, and balance;
- brake temperature/fade;
- calibrated assists.

Preserve the same `UCarSpecDataAsset`, input, telemetry, and race interfaces so this upgrade does not rewrite the game.

## Vehicle asset preparation

A production vehicle asset needs:

- licensed CAD or a licensed high-quality source model;
- correct dimensions and pivots;
- separate wheels, tires, brakes, steering, suspension, body, glass, cockpit, and emissive elements;
- a skeleton/rig that matches the movement setup;
- simple, stable physics collision separate from render geometry;
- interior and exterior material IDs;
- damage/LOD policy if later required;
- authored normal/tangent data and UVs;
- a provenance and license entry before import.

Do not infer a branded car's exact physics from marketing pages or another game's behavior.
