// Copyright RacingSim. All Rights Reserved.

using UnrealBuildTool;

/**
 * Automation specs for RacingSim.
 *
 * Declared "UncookedOnly" in RacingSim.uproject, which is the point of this module
 * existing at all: an UncookedOnly module is not built into a cooked Game or Client
 * target, so test code cannot ship. That is a stronger guarantee than wrapping tests
 * in WITH_AUTOMATION_TESTS inside the runtime module and trusting every future author
 * to remember the guard.
 *
 * CORE-001 requires this to be verified against the packaged artifact, not against
 * this file: RacingSimTests must appear in no staging manifest and in no "Mounting"
 * line of the packaged runtime log.
 */
public class RacingSimTests : ModuleRules
{
	public RacingSimTests(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Same reason as RacingSim.Build.cs: V7 build settings drop the module root
		// from the include path, and this module mirrors the layer folders.
		PrivateIncludePaths.Add(ModuleDirectory);

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine"
		});

		// The module under test. Tests depend on the runtime module; nothing in the
		// runtime module may ever depend on this one.
		//
		// ChaosVehicles is listed explicitly even though RacingSim already depends on
		// it publicly: VEH-002's specs call UChaosVehicleWheel::StaticClass() directly
		// (VehicleChassisSpec.cpp), and a module that directly references another
		// module's exported symbols must depend on it directly rather than relying on
		// a transitive re-export for linking.
		// AIModule is here for AAIController, which VEH-006's manoeuvre fixture uses to
		// POSSESS the vehicle pawn. That is not a convenience: an unpossessed Chaos
		// vehicle never receives its own input.
		// UChaosVehicleMovementComponent::UpdateState computes
		//     bProcessLocally = bRequiresControllerForInputs
		//         ? (Controller && Controller->IsLocalController())
		//         : true
		// (ChaosVehicleMovementComponent.cpp:1281), and bRequiresControllerForInputs
		// defaults true. When bProcessLocally is false the component ignores the
		// game-thread input entirely and takes its values from ReplicatedState instead,
		// which only the server RPC ServerUpdateState ever writes -- so in a test world
		// throttle would be silently discarded.
		//
		// AAIController rather than APlayerController because
		// APlayerController::IsLocalController() returns false with no NetDriver and no
		// ULocalPlayer, which a bare test world has neither of, while AController's own
		// implementation returns true for NM_Standalone. See VehicleManoeuvreFixture.h.
		//
		// Possession is NOT what keeps the chassis awake, and an earlier version of this
		// comment claimed it was. Sleep is a separate defect with a separate fix in
		// ARacingVehiclePawn::BeginPlay -- SetSleeping() cannot wake a pawn that has no
		// skeletal mesh, controller or not.
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"RacingSim",
			"ChaosVehicles",
			"AIModule",

			// UI-002: the HUD widget specs read UTextBlock/UWidget state.
			"UMG",
			"Slate",
			"SlateCore"
		});
	}
}
