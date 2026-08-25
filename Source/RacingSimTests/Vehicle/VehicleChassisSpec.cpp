// Copyright RacingSim. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Vehicle/PrototypeVehicleWheel.h"
#include "Vehicle/VehicleChassisDataAsset.h"

using namespace RacingSim::Validation;

/**
 * VEH-002: the prototype chassis DataAsset and its cross-check against the wheel
 * classes.
 *
 * ---------------------------------------------------------------------------
 * What this file deliberately does NOT test, and why
 * ---------------------------------------------------------------------------
 *
 * No ARacingVehiclePawn is spawned here. TRACK-001/TEST-001 recorded, and
 * TrackPrototypeLevelSpec.cpp's file header explains in full, that a SmokeFilter
 * test in this project's automation harness that constructs any non-template Actor
 * or UActorComponent crashes the whole run before RegisterEngineElements() has run
 * (Docs/Environment.md), producing no index.json rather than a failure. This file's
 * fixtures (UVehicleChassisDataAsset, a UDataAsset, and UChaosVehicleWheel CDOs
 * reached only through TSubclassOf::GetDefaultObject()) are safe for the same reason
 * VEH-001's VehicleInputConfigSpec.cpp is: neither path creates a component.
 *
 * A pawn-spawn integration test -- actually possessing the pawn, Ticking it, and
 * observing Chaos apply the wheel setups -- is real, missing coverage. It is left to
 * a later ticket rather than attempted here with the loaded-package technique
 * TrackPrototypeLevelSpec.cpp uses, because that technique needs a placed, versioned
 * test map and this ticket does not own one; inventing a throwaway one would be
 * content work outside VEH-002's scope. Recorded as a gap, not silently dropped.
 *
 * ---------------------------------------------------------------------------
 * CDOs are safe; fresh instances of the wheel classes are not attempted
 * ---------------------------------------------------------------------------
 *
 * UPrototypeFrontWheel::StaticClass()->GetDefaultObject() returns the class default
 * object, which was constructed once at module load, not freshly here -- it does not
 * re-run PostInitProperties and touches no typed-element registration. This is the
 * exact mechanism TrackDefinitionActorSpec.cpp already relies on for the same reason.
 */

namespace
{
	/** A chassis asset whose declared wheel geometry agrees with the wheel classes' CDOs. */
	UVehicleChassisDataAsset* MakeMatchingChassis()
	{
		UVehicleChassisDataAsset* Chassis = NewObject<UVehicleChassisDataAsset>(GetTransientPackage());
		// Defaults already agree with UPrototypeFrontWheel/UPrototypeRearWheel's CDO
		// values -- this fixture exists to make that agreement explicit and testable,
		// not to author new numbers.
		return Chassis;
	}
}

// ---------------------------------------------------------------------------
// Geometry helpers
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRacingSimVehicleChassisGeometryTest,
	"RacingSim.Vehicle.ChassisGeometry",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FRacingSimVehicleChassisGeometryTest::RunTest(const FString& Parameters)
{
	UVehicleChassisDataAsset* Chassis = MakeMatchingChassis();

	// Wheelbase is derived, not authored -- see the header. Default: 140 - (-145) = 285.
	TestEqual(TEXT("Wheelbase is the derived front-minus-rear offset"),
		Chassis->GetWheelbaseCm(), 285.0f);

	// +X forward, +Y right, +Z up (Unreal left-handed Z-up). A right-hand wheel is at
	// +half track; a front wheel is at +FrontAxleOffsetXCm.
	const FVector FrontRight = Chassis->GetWheelOffsetCm(EVehicleWheelIndex::FrontRight);
	TestEqual(TEXT("Front-right wheel sits at +FrontAxleOffsetXCm along X"), static_cast<float>(FrontRight.X), 140.0f);
	TestTrue(TEXT("Front-right wheel is on the +Y (right) side"), FrontRight.Y > 0.0);

	const FVector RearLeft = Chassis->GetWheelOffsetCm(EVehicleWheelIndex::RearLeft);
	TestEqual(TEXT("Rear-left wheel sits at RearAxleOffsetXCm along X"), static_cast<float>(RearLeft.X), -145.0f);
	TestTrue(TEXT("Rear-left wheel is on the -Y (left) side"), RearLeft.Y < 0.0);

	TestTrue(TEXT("Front wheels are classified as front"),
		UVehicleChassisDataAsset::IsFrontWheel(EVehicleWheelIndex::FrontLeft));
	TestFalse(TEXT("Rear wheels are not classified as front"),
		UVehicleChassisDataAsset::IsFrontWheel(EVehicleWheelIndex::RearRight));

	TestEqual(TEXT("NumPrototypeVehicleWheels is the fixed Phase 1 wheel count"),
		NumPrototypeVehicleWheels, 4);

	return true;
}

// ---------------------------------------------------------------------------
// Relationship validation -- the checks no per-field clamp can express
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRacingSimVehicleChassisRelationshipsTest,
	"RacingSim.Vehicle.ChassisRelationships",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FRacingSimVehicleChassisRelationshipsTest::RunTest(const FString& Parameters)
{
	// A clean default chassis must report no issues.
	{
		UVehicleChassisDataAsset* Chassis = MakeMatchingChassis();
		const FRacingValidationResult Result = Chassis->ValidateReadOnly();
		TestTrue(TEXT("A clean default chassis validates with no issues"), Result.Issues.IsEmpty());
	}

	// 1. Front axle behind rear axle -- a geometric contradiction no clamp catches.
	{
		UVehicleChassisDataAsset* Chassis = MakeMatchingChassis();
		Chassis->FrontAxleOffsetXCm = -50.0f;
		Chassis->RearAxleOffsetXCm = 50.0f;
		const FRacingValidationResult Result = Chassis->ValidateReadOnly();
		bool bFound = false;
		for (const FRacingValidationIssue& Issue : Result.Issues)
		{
			bFound |= (Issue.PropertyName == TEXT("FrontAxleOffsetXCm"));
		}
		TestTrue(TEXT("Front axle behind rear axle is reported by name"), bFound);
	}

	// 2. Track narrower than the tyre -- wheels would interpenetrate.
	{
		UVehicleChassisDataAsset* Chassis = MakeMatchingChassis();
		Chassis->FrontTrackWidthCm = 10.0f;
		Chassis->FrontWheelWidthCm = 24.0f;
		const FRacingValidationResult Result = Chassis->ValidateReadOnly();
		bool bFound = false;
		for (const FRacingValidationIssue& Issue : Result.Issues)
		{
			bFound |= (Issue.PropertyName == TEXT("FrontTrackWidthCm"));
		}
		TestTrue(TEXT("Front track narrower than the front tyre is reported"), bFound);
	}

	// 5. Centre of mass above the wheel tops -- the rollover case.
	{
		UVehicleChassisDataAsset* Chassis = MakeMatchingChassis();
		Chassis->CentreOfMassOffsetZCm = 50.0f;
		Chassis->WheelCentreHeightCm = -35.0f;
		const FRacingValidationResult Result = Chassis->ValidateReadOnly();
		bool bFound = false;
		for (const FRacingValidationIssue& Issue : Result.Issues)
		{
			bFound |= (Issue.PropertyName == TEXT("CentreOfMassOffsetZCm"));
		}
		TestTrue(TEXT("Centre of mass above the wheel tops is reported"), bFound);
	}

	// 6. Torque split reported as meaningless outside AWD, not silently accepted.
	{
		UVehicleChassisDataAsset* Chassis = MakeMatchingChassis();
		Chassis->DrivetrainLayout = EVehicleDrivetrainLayout::RearWheelDrive;
		Chassis->FrontRearTorqueSplit = 0.3f;
		const FRacingValidationResult Result = Chassis->ValidateReadOnly();
		bool bFound = false;
		for (const FRacingValidationIssue& Issue : Result.Issues)
		{
			bFound |= (Issue.PropertyName == TEXT("FrontRearTorqueSplit"));
		}
		TestTrue(TEXT("A non-default torque split under RWD is reported as meaningless"), bFound);
	}

	// ValidateReadOnly must never mutate the asset it was called on.
	{
		UVehicleChassisDataAsset* Chassis = MakeMatchingChassis();
		Chassis->MassKg = 99999.0f; // out of range; would be corrected under Validate(true)
		Chassis->ValidateReadOnly();
		TestEqual(TEXT("ValidateReadOnly does not mutate the asset it validates"), Chassis->MassKg, 99999.0f);
	}

	return true;
}

// ---------------------------------------------------------------------------
// Wheel class CDOs -- geometry Chaos actually reads at SetupVehicle time
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRacingSimVehicleWheelClassesTest,
	"RacingSim.Vehicle.WheelClasses",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FRacingSimVehicleWheelClassesTest::RunTest(const FString& Parameters)
{
	const UChaosVehicleWheel* FrontCdo = UPrototypeFrontWheel::StaticClass()->GetDefaultObject<UChaosVehicleWheel>();
	const UChaosVehicleWheel* RearCdo = UPrototypeRearWheel::StaticClass()->GetDefaultObject<UChaosVehicleWheel>();

	TestTrue(TEXT("Front wheel is steered"), FrontCdo->bAffectedBySteering);
	TestFalse(TEXT("Front wheel is not handbraked"), FrontCdo->bAffectedByHandbrake);
	TestFalse(TEXT("Front wheel is not driven under the Phase 1 RWD CDO default"), FrontCdo->bAffectedByEngine);

	TestFalse(TEXT("Rear wheel is not steered"), RearCdo->bAffectedBySteering);
	TestTrue(TEXT("Rear wheel is handbraked"), RearCdo->bAffectedByHandbrake);
	TestTrue(TEXT("Rear wheel is driven under the Phase 1 RWD CDO default"), RearCdo->bAffectedByEngine);

	TestTrue(TEXT("Front and rear wheels both have positive brake torque"),
		FrontCdo->MaxBrakeTorque > 0.0f && RearCdo->MaxBrakeTorque > 0.0f);
	TestTrue(TEXT("Rear (handbraked) wheel has positive handbrake torque"),
		RearCdo->MaxHandBrakeTorque > 0.0f);
	TestEqual(TEXT("Front (non-handbraked) wheel has zero handbrake torque"),
		FrontCdo->MaxHandBrakeTorque, 0.0f);

	// AxleType, not bAffectedByEngine, is what the differential actually reads --
	// repair cycle 1 (code-reviewer HIGH-2): the first pass set only
	// bAffectedByEngine, which the engine ignores once AxleType is defined, so
	// DrivetrainLayout's mapping onto EVehicleDifferential had no effect at all.
	// AxleType read directly (not via GetAxleType(), which is non-const) since these
	// CDOs are held as const UChaosVehicleWheel* here.
	TestEqual(TEXT("Front wheel's AxleType is Front, which the differential reads"),
		FrontCdo->AxleType, EAxleType::Front);
	TestEqual(TEXT("Rear wheel's AxleType is Rear, which the differential reads"),
		RearCdo->AxleType, EAxleType::Rear);

	return true;
}

// ---------------------------------------------------------------------------
// Chassis-vs-wheel-class cross-validation -- the check the DataAsset cannot run on itself
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRacingSimVehicleChassisWheelMatchTest,
	"RacingSim.Vehicle.ChassisWheelMatch",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FRacingSimVehicleChassisWheelMatchTest::RunTest(const FString& Parameters)
{
	// The default chassis and the prototype wheel classes must agree -- this is the
	// invariant PrototypeVehicleWheel.h exists to enforce, proven rather than assumed.
	{
		UVehicleChassisDataAsset* Chassis = MakeMatchingChassis();
		const FRacingValidationResult Result = RacingSim::Vehicle::ValidateChassisAgainstWheelClasses(
			Chassis, UPrototypeFrontWheel::StaticClass(), UPrototypeRearWheel::StaticClass());
		TestTrue(TEXT("The default chassis and wheel classes agree on geometry"), Result.Issues.IsEmpty());
	}

	// A disagreement must be caught, not silently accepted -- the whole point of the
	// check per the header ("a disagreement is a validation failure, never a silent
	// write").
	{
		UVehicleChassisDataAsset* Chassis = MakeMatchingChassis();
		Chassis->FrontWheelRadiusCm = 50.0f; // CDO is 34.0f
		const FRacingValidationResult Result = RacingSim::Vehicle::ValidateChassisAgainstWheelClasses(
			Chassis, UPrototypeFrontWheel::StaticClass(), UPrototypeRearWheel::StaticClass());
		bool bFound = false;
		for (const FRacingValidationIssue& Issue : Result.Issues)
		{
			bFound |= (Issue.PropertyName == TEXT("FrontWheelRadiusCm"));
		}
		TestTrue(TEXT("A chassis radius that disagrees with the wheel CDO is caught"), bFound);
	}

	// MaxSteerAngleDegrees is cross-checked the same way as radius/width -- repair
	// cycle 1 (code-reviewer MEDIUM-2): the mechanical lock is the one geometry field
	// the first pass declared but never validated or applied.
	{
		UVehicleChassisDataAsset* Chassis = MakeMatchingChassis();
		Chassis->MaxSteerAngleDegrees = 55.0f; // CDO is 40.0f
		const FRacingValidationResult Result = RacingSim::Vehicle::ValidateChassisAgainstWheelClasses(
			Chassis, UPrototypeFrontWheel::StaticClass(), UPrototypeRearWheel::StaticClass());
		bool bFound = false;
		for (const FRacingValidationIssue& Issue : Result.Issues)
		{
			bFound |= (Issue.PropertyName == TEXT("MaxSteerAngleDegrees"));
		}
		TestTrue(TEXT("A chassis MaxSteerAngleDegrees that disagrees with the wheel CDO is caught"), bFound);
	}

	// Null inputs are reported as failures, never crash.
	{
		const FRacingValidationResult NullChassis = RacingSim::Vehicle::ValidateChassisAgainstWheelClasses(
			nullptr, UPrototypeFrontWheel::StaticClass(), UPrototypeRearWheel::StaticClass());
		TestFalse(TEXT("A null chassis is reported, not a crash"), NullChassis.Issues.IsEmpty());

		UVehicleChassisDataAsset* Chassis = MakeMatchingChassis();
		const FRacingValidationResult NullFront = RacingSim::Vehicle::ValidateChassisAgainstWheelClasses(
			Chassis, nullptr, UPrototypeRearWheel::StaticClass());
		TestFalse(TEXT("A null front wheel class is reported, not a crash"), NullFront.Issues.IsEmpty());
	}

	return true;
}
