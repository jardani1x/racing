// Copyright RacingSim. All Rights Reserved.

#include "Core/RacingSimValidation.h"
#include "Misc/AutomationTest.h"
#include "Vehicle/VehicleCameraDataAsset.h"

using namespace RacingSim::Validation;

/**
 * VEH-005: UVehicleCameraDataAsset's range table, relationship check, and the
 * FVehicleCameraSettings this asset assembles.
 *
 * Added on code review (VEH-005 HIGH-2): every sibling DataAsset in this module carries
 * this exact anti-drift assertion (VehicleTuneSpec.cpp, VehicleFailureDetectionSpec.cpp,
 * VehicleInputConfigSpec.cpp) because StaticRanges() is the ONLY enforcement path in a
 * packaged Game build -- WITH_METADATA is WITH_EDITORONLY_DATA, so ClampMin/ClampMax is 0
 * there, per CORE-003. This asset shipped without the test that keeps its hand-written
 * table from silently drifting from the header's UPROPERTY metadata.
 *
 * A UDataAsset has no components, so this file is Smoke-safe for the same reason
 * VehicleTuneSpec.cpp is: FEngineLoop::PreInit runs before RegisterEngineElements(), but
 * nothing here constructs an Actor or UActorComponent.
 */

namespace
{
	UVehicleCameraDataAsset* MakeDefaultCamera()
	{
		return NewObject<UVehicleCameraDataAsset>(GetTransientPackage());
	}

	bool HasCameraIssueFor(const FRacingValidationResult& Result, const FName PropertyName)
	{
		return Result.Issues.ContainsByPredicate(
			[PropertyName](const FRacingValidationIssue& Issue) { return Issue.PropertyName == PropertyName; });
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRacingSimVehicleCameraDataAssetTest,
	"RacingSim.Vehicle.CameraDataAsset",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FRacingSimVehicleCameraDataAssetTest::RunTest(const FString& Parameters)
{
	// -- StaticRanges must mirror the UPROPERTY metadata in both directions --------
	const TConstArrayView<FRacingPropertyRange> Ranges = UVehicleCameraDataAsset::StaticRanges();
	TestTrue(TEXT("The camera range table is not empty"), Ranges.Num() > 0);

	const FRacingValidationResult Match =
		VerifyRangesMatchMetadata(UVehicleCameraDataAsset::StaticClass(), Ranges);
	for (const FRacingValidationIssue& Issue : Match.Issues)
	{
		AddError(FString::Printf(TEXT("StaticRanges disagrees with UPROPERTY metadata on '%s': %s"),
			*Issue.PropertyName.ToString(), *Issue.Message));
	}
	TestTrue(TEXT("StaticRanges and the UPROPERTY clamp metadata agree in both directions"),
		Match.IsClean());

	// -- A default asset validates clean --------------------------------------------
	UVehicleCameraDataAsset* Default = MakeDefaultCamera();
	TestTrue(TEXT("A freshly constructed camera asset validates clean"),
		Default->ValidateReadOnly().IsClean());

	// -- Out-of-range numerics are reported, corrected only when asked --------------
	{
		UVehicleCameraDataAsset* Camera = MakeDefaultCamera();
		Camera->ArmLengthCm = 99999.0f;      // above ClampMax
		Camera->SpeedForMaxFovBoostCms = 0.0f; // below ClampMin: the documented NaN-divisor case

		const FRacingValidationResult Report = Camera->ValidateReadOnly();
		TestFalse(TEXT("An out-of-range camera asset does not validate clean"), Report.IsClean());
		TestTrue(TEXT("ArmLengthCm above its maximum is reported"), HasCameraIssueFor(Report, TEXT("ArmLengthCm")));
		TestTrue(TEXT("SpeedForMaxFovBoostCms below its minimum is reported"),
			HasCameraIssueFor(Report, TEXT("SpeedForMaxFovBoostCms")));

		TestEqual(TEXT("A read-only validation never mutates the asset"), Camera->ArmLengthCm, 99999.0f);

		Camera->Validate(/*bCorrect=*/true);
		TestTrue(TEXT("Validate(true) corrects ArmLengthCm back into range"),
			Camera->ArmLengthCm >= 50.0f && Camera->ArmLengthCm <= 5000.0f);
	}

	// -- The FOV-ceiling relationship is reported and never auto-corrected ----------
	{
		UVehicleCameraDataAsset* Camera = MakeDefaultCamera();
		Camera->BaseFieldOfViewDegrees = 165.0f;
		Camera->MaxFieldOfViewBoostDegrees = 20.0f; // sum is 185, past UCameraComponent's 170 ceiling

		const FRacingValidationResult Report = Camera->ValidateReadOnly();
		TestTrue(TEXT("Base + boost past the 170-degree ceiling is reported"),
			HasCameraIssueFor(Report, TEXT("MaxFieldOfViewBoostDegrees")));

		Camera->Validate(/*bCorrect=*/true);
		TestEqual(TEXT("Validate(true) does not correct the ceiling relationship, same policy as VEH-004"),
			Camera->MaxFieldOfViewBoostDegrees, 20.0f);
	}

	// -- GetSettings() maps every field, not a transposed subset --------------------
	{
		UVehicleCameraDataAsset* Camera = MakeDefaultCamera();
		Camera->ArmLengthCm = 700.0f;
		Camera->SocketHeightCm = 260.0f;
		Camera->SocketForwardOffsetCm = -15.0f;
		Camera->CameraPitchDegrees = -12.0f;
		Camera->CameraLagSpeed = 8.0f;
		Camera->CameraRotationLagSpeed = 6.0f;
		Camera->BaseFieldOfViewDegrees = 95.0f;
		Camera->MaxFieldOfViewBoostDegrees = 12.0f;
		Camera->SpeedForMaxFovBoostCms = 2500.0f;

		const FVehicleCameraSettings Settings = Camera->GetSettings();
		TestEqual(TEXT("GetSettings maps ArmLengthCm"), Settings.ArmLengthCm, Camera->ArmLengthCm);
		TestEqual(TEXT("GetSettings maps SocketHeightCm"), Settings.SocketHeightCm, Camera->SocketHeightCm);
		TestEqual(TEXT("GetSettings maps SocketForwardOffsetCm"), Settings.SocketForwardOffsetCm, Camera->SocketForwardOffsetCm);
		TestEqual(TEXT("GetSettings maps CameraPitchDegrees"), Settings.CameraPitchDegrees, Camera->CameraPitchDegrees);
		TestEqual(TEXT("GetSettings maps CameraLagSpeed"), Settings.CameraLagSpeed, Camera->CameraLagSpeed);
		TestEqual(TEXT("GetSettings maps CameraRotationLagSpeed"), Settings.CameraRotationLagSpeed, Camera->CameraRotationLagSpeed);
		TestEqual(TEXT("GetSettings maps BaseFieldOfViewDegrees"), Settings.BaseFieldOfViewDegrees, Camera->BaseFieldOfViewDegrees);
		TestEqual(TEXT("GetSettings maps MaxFieldOfViewBoostDegrees"), Settings.MaxFieldOfViewBoostDegrees, Camera->MaxFieldOfViewBoostDegrees);
		TestEqual(TEXT("GetSettings maps SpeedForMaxFovBoostCms"), Settings.SpeedForMaxFovBoostCms, Camera->SpeedForMaxFovBoostCms);
	}

	// -- Defaults pin: a null CameraAsset falls back to FVehicleCameraSettings()'s own
	// defaults (ARacingVehiclePawn::ResolveCameraSettings). Nothing kept these two
	// agreeing until now -- the same defaults-pin VEH-004 has for
	// UVehicleFailureThresholdsDataAsset (RacingSim.Vehicle.FailureThresholdDefaultsMatchAsset).
	{
		UVehicleCameraDataAsset* Default2 = MakeDefaultCamera();
		const FVehicleCameraSettings AssetDefaults = Default2->GetSettings();
		const FVehicleCameraSettings StructDefaults;

		TestEqual(TEXT("Defaults pin: ArmLengthCm"), AssetDefaults.ArmLengthCm, StructDefaults.ArmLengthCm);
		TestEqual(TEXT("Defaults pin: SocketHeightCm"), AssetDefaults.SocketHeightCm, StructDefaults.SocketHeightCm);
		TestEqual(TEXT("Defaults pin: SocketForwardOffsetCm"), AssetDefaults.SocketForwardOffsetCm, StructDefaults.SocketForwardOffsetCm);
		TestEqual(TEXT("Defaults pin: CameraPitchDegrees"), AssetDefaults.CameraPitchDegrees, StructDefaults.CameraPitchDegrees);
		TestEqual(TEXT("Defaults pin: CameraLagSpeed"), AssetDefaults.CameraLagSpeed, StructDefaults.CameraLagSpeed);
		TestEqual(TEXT("Defaults pin: CameraRotationLagSpeed"), AssetDefaults.CameraRotationLagSpeed, StructDefaults.CameraRotationLagSpeed);
		TestEqual(TEXT("Defaults pin: BaseFieldOfViewDegrees"), AssetDefaults.BaseFieldOfViewDegrees, StructDefaults.BaseFieldOfViewDegrees);
		TestEqual(TEXT("Defaults pin: MaxFieldOfViewBoostDegrees"), AssetDefaults.MaxFieldOfViewBoostDegrees, StructDefaults.MaxFieldOfViewBoostDegrees);
		TestEqual(TEXT("Defaults pin: SpeedForMaxFovBoostCms"), AssetDefaults.SpeedForMaxFovBoostCms, StructDefaults.SpeedForMaxFovBoostCms);
	}

	return true;
}
