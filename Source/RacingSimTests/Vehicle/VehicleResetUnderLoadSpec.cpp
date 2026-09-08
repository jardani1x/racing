// Copyright RacingSim. All Rights Reserved.

#include "VehicleManoeuvreFixture.h"

#include "Race/TrackDefinitionActor.h"
#include "Vehicle/RacingVehiclePawn.h"
#include "Vehicle/VehicleChassisDataAsset.h"
#include "Vehicle/VehicleFailureDetection.h"
#include "Vehicle/VehicleInputComponent.h"
#include "Vehicle/VehicleInputProcessor.h"
#include "Vehicle/VehicleTelemetryTypes.h"

#include "Components/SplineComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"

/**
 * VEH-005 Gate B/C, deferred to VEH-006 and discharged here: ARacingVehiclePawn::
 * ExecuteSafeReset, driven through a real world, a real track and a real Chaos car
 * that is genuinely LOADED at the moment of the reset.
 *
 * ---------------------------------------------------------------------------
 * Why this could not be a unit test, and why VEH-005 shipped without it
 * ---------------------------------------------------------------------------
 *
 * ExecuteSafeReset's arithmetic is already covered: RacingSim.Vehicle.ResetMath pins
 * ResolveGroundCorrectedResetZCm and IsResetDistanceAtOrBeforeQuery, and
 * RacingSim.Race.TrackDefinitionActor pins GetResetPoseAtOrBeforeDistanceCm against
 * real spline geometry. None of that can fail if the function is wired wrong, because
 * none of it touches the four things this test exists for:
 *
 *   1. the TELEPORT actually lands where the track said, on a car whose physics state
 *      is mid-manoeuvre rather than at rest;
 *   2. the car ends up STOPPED and UPRIGHT, not merely relocated -- ResetVehicle() and
 *      the explicit velocity clears are two separate calls and either could be dropped
 *      without a unit test noticing;
 *   3. the failure detector stays SILENT across the reset. That is not a formality:
 *      until the fix in this same ticket, NotifyTelemetryDiscontinuity was a no-op in
 *      the only respect that mattered, so every safe reset raised Error-severity
 *      Tunnelling and then InvalidContact. A reset that trips the crash detector is a
 *      reset that will invalidate a race the moment anything uses that signal;
 *   4. the shaped input command comes back NEUTRAL, so a car reset at full throttle and
 *      full lock does not resume at full throttle and full lock.
 *
 * VEH-005 could not test any of it because no world in this harness stepped physics.
 * VEH-006's FVehicleManoeuvreFixture is what changed, and this spec is the first thing
 * to spawn an ATrackDefinitionActor into that world rather than borrowing the class
 * default object the way the RACE specs must.
 *
 * ---------------------------------------------------------------------------
 * Why a spawned track, not the CDO
 * ---------------------------------------------------------------------------
 *
 * TrackDefinitionActorSpec.cpp's file comment records two rejected approaches -- a bare
 * NewObject (dies in the Typed Element Framework) and a hand-rolled UWorld::CreateWorld
 * (dies inside CreateWorld itself) -- and settles on mutating the process-wide CDO,
 * with an exhaustive snapshot/restore fixture to make that safe. That was the right
 * call for a SmokeFilter suite with no world available.
 *
 * It is the wrong call here, and the reason is worth stating rather than leaving as a
 * silent divergence: this suite already has a world, because ExecuteSafeReset line
 * traces against it. Given a world, World->SpawnActor is the supported path the CDO
 * approach was working around, it exercises OnConstruction and BeginPlay (which the CDO
 * approach explicitly cannot -- see "WHAT THIS COSTS IN COVERAGE" in that file), and it
 * leaves no shared state behind for whatever suite runs next. The cost is ProductFilter
 * rather than SmokeFilter, which this suite pays anyway for needing a world at all.
 *
 * ---------------------------------------------------------------------------
 * Units
 * ---------------------------------------------------------------------------
 *
 * Centimetres and centimetres per second throughout, per Core/RacingSimUnits.h.
 */

namespace VehicleResetUnderLoadPrivate
{
	/**
	 * A 50 m radius closed circle, centred on the world origin.
	 *
	 * Sized against FVehicleManoeuvreFixture::GroundHalfSizeCm (10000 cm): every point
	 * on the centreline, and therefore every reset pose, lands on the ground slab. A
	 * larger circle would put reset samples off the edge, where ExecuteSafeReset's
	 * ground trace finds nothing and silently falls back to the seed height -- the test
	 * would still pass, while quietly no longer testing the trace.
	 */
	constexpr double TrackRadiusCm = 5000.0;

	/** Spline control points. Twelve is what TrackDefinitionActorSpec uses for its own circle. */
	constexpr int32 TrackSplinePoints = 12;

	/** The fixed step, matching the rest of VEH-006. */
	constexpr float StepSeconds = 1.0f / 60.0f;

	/** Full throttle and full right lock, held long enough to load the car properly. */
	constexpr int32 LoadSteps = 180;

	/**
	 * The progress distance handed to ExecuteSafeReset, centimetres.
	 *
	 * Deliberately NOT a multiple of ATrackDefinitionActor::ResetSampleSpacingCm (2500):
	 * the whole contract is "at or before", so a query that lands exactly on a sample
	 * would pass even if the search rounded the wrong way.
	 */
	constexpr double RequestedProgressCm = 12000.0;

	/**
	 * Author the circle onto a spawned track and bake it.
	 *
	 * @return the track, or null having already reported why.
	 */
	ATrackDefinitionActor* SpawnBuiltTrack(FAutomationTestBase& Test, UWorld* World)
	{
		if (World == nullptr)
		{
			Test.AddError(TEXT("SpawnBuiltTrack was given a null world."));
			return nullptr;
		}

		// DEFERRED, and this is not a style preference.
		//
		// FVehicleManoeuvreFixture calls BeginPlayInTestWorld() before anything is
		// spawned, so the world has already begun play and SpawnActor dispatches
		// BeginPlay before it returns -- before this function can author a single
		// property. ATrackDefinitionActor::BeginPlay rebuilds the track data and then
		// validates it, so a non-deferred spawn bakes the DEFAULT two-point spline with
		// TrackId still None and logs, at Error severity:
		//
		//     Track 'None' failed validation at BeginPlay: TrackId is None.
		//
		// which the automation framework turns into a test failure, preceded by a gate
		// clamp warning about a 200 cm lap. Author first, finish second. The pawn is
		// spawned exactly this way, for exactly this reason.
		ATrackDefinitionActor* Track = World->SpawnActorDeferred<ATrackDefinitionActor>(
			ATrackDefinitionActor::StaticClass(), FTransform::Identity);
		if (Track == nullptr)
		{
			Test.AddError(TEXT("SpawnActorDeferred<ATrackDefinitionActor> returned null."));
			return nullptr;
		}

		USplineComponent* Spline = Track->GetCenterlineSpline();
		if (Spline == nullptr)
		{
			Test.AddError(TEXT("The spawned track has no centerline spline component."));
			return nullptr;
		}

		TArray<FVector> Points;
		Points.Reserve(TrackSplinePoints);
		for (int32 Index = 0; Index < TrackSplinePoints; ++Index)
		{
			const double Angle = 2.0 * UE_DOUBLE_PI * static_cast<double>(Index) / static_cast<double>(TrackSplinePoints);
			Points.Add(FVector(TrackRadiusCm * FMath::Cos(Angle), TrackRadiusCm * FMath::Sin(Angle), 0.0));
		}

		Spline->SetClosedLoop(true, /*bUpdateSpline*/ false);
		Spline->SetSplinePoints(Points, ESplineCoordinateSpace::Local, /*bUpdateSpline*/ true);

		Track->TrackId = FName(TEXT("Track.Test.ResetUnderLoad"));

		// Only now does BeginPlay run, on a named track carrying the authored circle.
		UGameplayStatics::FinishSpawningActor(Track, FTransform::Identity);

		// BeginPlay already baked. Rebuilding is still worth the call: it is the only
		// way to get a BOOLEAN answer out of the bake, and a bake that failed inside
		// BeginPlay would otherwise be visible here only as an empty sample array.
		if (!Track->RebuildTrackData())
		{
			Test.AddError(TEXT("ATrackDefinitionActor::RebuildTrackData() failed on the authored circle."));
			return nullptr;
		}

		// Asserted rather than assumed. ExecuteSafeReset's documented no-op on an
		// unbuilt centreline means a track that failed to bake produces a test that
		// passes every "the car did not blow up" assertion by never moving the car at
		// all -- the exact false green this suite must not be able to report.
		if (!Track->IsTrackDataBuilt())
		{
			Test.AddError(TEXT("RebuildTrackData() reported success but IsTrackDataBuilt() is false."));
			return nullptr;
		}

		if (Track->GetNumResetSamples() <= 0)
		{
			Test.AddError(FString::Printf(
				TEXT("The built track has %d reset samples; ExecuteSafeReset would be a documented no-op."),
				Track->GetNumResetSamples()));
			return nullptr;
		}

		return Track;
	}
}

/**
 * The load case: a car at speed, at full lock, reset onto the track.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVehicleSafeResetUnderLoadTest,
	"RacingSim.Vehicle.Manoeuvre.SafeResetUnderLoad",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::ProductFilter)

bool FVehicleSafeResetUnderLoadTest::RunTest(const FString& Parameters)
{
	using namespace VehicleResetUnderLoadPrivate;

	// Telemetry ON. The detector staying silent across the reset is a stated criterion,
	// and it cannot be observed with capture disabled. No AddExpectedError is registered
	// on purpose: any Error-severity failure the detector raises here MUST fail this
	// test, which is precisely how the NotifyTelemetryDiscontinuity no-op was found.
	FVehicleManoeuvreFixture Fixture;
	if (!Fixture.Setup(*this, /*bEnableTelemetry*/ true))
	{
		return false;
	}

	ARacingVehiclePawn* Pawn = Fixture.GetPawn();
	ATrackDefinitionActor* Track = SpawnBuiltTrack(*this, Fixture.GetWorld());
	if (Pawn == nullptr || Track == nullptr)
	{
		return false;
	}

	// ------------------------------------------------------------------
	// LOAD THE CAR. A reset from rest proves almost nothing.
	// ------------------------------------------------------------------
	//
	// Full throttle and full right lock for 3 s: the car is moving, yawing, with
	// weight transferred onto the outside wheels and the input rate limiter saturated.
	// That is the state a driver is actually in when they hit reset, and it is the
	// state in which "teleport, then zero the velocities" can go wrong -- a residual
	// angular velocity survives a linear-only clear, and a missing ResetVehicle()
	// leaves the wheels spinning.
	Fixture.Drive(FVehicleManoeuvreFixture::ThrottleSample(1.0, 1.0), LoadSteps, StepSeconds);

	const double LoadedSpeedCms = Fixture.GetForwardSpeedCms();
	const double LoadedYawRate = Fixture.GetYawRateDegreesPerSecond();

	// The precondition, checked. A "reset stopped the car" assertion is vacuous if the
	// car was never going anywhere, and this fixture's own history is a long list of
	// silently stationary cars.
	TestTrue(
		FString::Printf(TEXT("The car is genuinely loaded before the reset: %.2f cm/s forward, %.3f deg/s yaw"),
			LoadedSpeedCms, LoadedYawRate),
		LoadedSpeedCms > 500.0 && FMath::Abs(LoadedYawRate) > 5.0);

	TestFalse(TEXT("No failure is reported while merely driving hard"),
		Pawn->GetLastFailureReport().HasAnyFailure());

	// What the track itself says the answer is. Asserting against this rather than
	// against re-derived arithmetic keeps the test about ExecuteSafeReset's WIRING; if
	// the pose maths is wrong, that is TrackDefinitionActorSpec's failure to report.
	int32 ExpectedSampleIndex = INDEX_NONE;
	double ExpectedSampleDistanceCm = 0.0;
	const FTransform ExpectedSeed =
		Track->GetResetPoseAtOrBeforeDistanceCm(RequestedProgressCm, ExpectedSampleIndex, ExpectedSampleDistanceCm);

	// TestTrue, not TestNotEqual: INDEX_NONE is an untyped literal and the overload set
	// cannot pick between the int32 and int64 forms, which is a compile error rather
	// than a silent wrong answer, but still a compile error.
	TestTrue(
		FString::Printf(TEXT("The track resolves a real reset sample for the requested distance, got index %d"),
			ExpectedSampleIndex),
		ExpectedSampleIndex != INDEX_NONE);
	TestTrue(
		FString::Printf(TEXT("The resolved reset distance %.2f cm is at or before the requested %.2f cm"),
			ExpectedSampleDistanceCm, RequestedProgressCm),
		ExpectedSampleDistanceCm <= RequestedProgressCm);

	// ------------------------------------------------------------------
	// THE RESET.
	// ------------------------------------------------------------------
	//
	// Null LapTracker: RACE-002 owns what a reset does to lap progress and tests it
	// directly (RacingSim.Race.LapTracker). Passing one here would couple this suite to
	// that contract without adding coverage of it, and IsValid(nullptr) is the
	// documented path.
	Pawn->ExecuteSafeReset(Track, /*LapTracker*/ nullptr, RequestedProgressCm);

	const FVector PostResetLocation = Pawn->GetActorLocation();

	TestTrue(TEXT("The post-reset pose is finite"),
		PostResetLocation.ContainsNaN() == false && Pawn->GetActorRotation().ContainsNaN() == false);

	// Lateral placement, against the track's own answer. 1 cm: the only thing between
	// the seed and the final location is the Z ground correction, which moves nothing
	// horizontally, so any XY drift at all is a bug rather than tolerance.
	const FVector ExpectedSeedLocation = ExpectedSeed.GetLocation();
	TestTrue(
		FString::Printf(TEXT("The car is placed at the track's own reset sample in XY: got (%.2f, %.2f), expected (%.2f, %.2f)"),
			PostResetLocation.X, PostResetLocation.Y, ExpectedSeedLocation.X, ExpectedSeedLocation.Y),
		FVector::Dist2D(PostResetLocation, ExpectedSeedLocation) <= 1.0);

	// Height, against the ground the trace actually hit. The fixture's slab has its top
	// surface at Z = 0, so a successful trace resolves to exactly the clearance.
	// Asserting the NUMBER rather than "above zero" is what distinguishes a working
	// trace from the documented fallback, which would also leave the car above ground.
	//
	// The clearance is the LARGER of the track's authored lift and this chassis's own
	// contact-patch depth, and on this fixture the chassis wins: PoseHeightOffsetCm
	// defaults to 50 cm while the prototype's tyres reach 70 cm below the actor origin.
	// Deriving the expectation from the same asset the pawn reads, rather than writing
	// 70 here, is what keeps this assertion honest if the geometry is retuned.
	const UVehicleChassisDataAsset* Chassis = Fixture.GetChassisAsset();
	double ChassisClearanceCm = 0.0;
	if (Chassis != nullptr)
	{
		for (int32 CornerIndex = 0; CornerIndex < NumPrototypeVehicleWheels; ++CornerIndex)
		{
			const EVehicleWheelIndex WheelIndex = static_cast<EVehicleWheelIndex>(CornerIndex);
			ChassisClearanceCm = FMath::Max(
				ChassisClearanceCm,
				-static_cast<double>(Chassis->GetWheelOffsetCm(WheelIndex).Z)
					+ static_cast<double>(Chassis->GetWheelRadiusCm(WheelIndex)));
		}
	}
	const double ExpectedClearanceCm = FMath::Max(Track->PoseHeightOffsetCm, ChassisClearanceCm);

	TestTrue(
		FString::Printf(TEXT("The ground trace placed the car one clearance above the slab: Z %.2f cm, expected %.2f cm"),
			PostResetLocation.Z, ExpectedClearanceCm),
		FMath::Abs(PostResetLocation.Z - ExpectedClearanceCm) <= 1.0);

	// The point of the maximum, asserted rather than assumed. A regression that dropped
	// the chassis term would still satisfy every other assertion in this test -- the car
	// would be placed, stopped and upright, just 20 cm inside the road, and the only
	// symptom would be the RunawayEnergy spike as the suspension ejected it.
	TestTrue(
		FString::Printf(TEXT("The reset clears this chassis's own contact patch: Z %.2f cm >= %.2f cm"),
			PostResetLocation.Z, ChassisClearanceCm),
		PostResetLocation.Z + 1.0 >= ChassisClearanceCm);

	// Upright. A reset that lands the car on its roof satisfies every distance
	// assertion above and is useless.
	const double UpDot = Pawn->GetActorUpVector().Z;
	TestTrue(FString::Printf(TEXT("The car is upright after the reset: up.Z = %.4f"), UpDot), UpDot > 0.99);

	// Stopped. Both halves, because they are cleared by two different calls:
	// ResetVehicle() for the drivetrain, SetPhysicsLinear/AngularVelocity for the body.
	TestTrue(
		FString::Printf(TEXT("Forward speed is ~zero immediately after the reset: %.4f cm/s (was %.2f)"),
			Fixture.GetForwardSpeedCms(), LoadedSpeedCms),
		FMath::Abs(Fixture.GetForwardSpeedCms()) <= 1.0);
	TestTrue(
		FString::Printf(TEXT("Yaw rate is ~zero immediately after the reset: %.4f deg/s (was %.3f)"),
			Fixture.GetYawRateDegreesPerSecond(), LoadedYawRate),
		FMath::Abs(Fixture.GetYawRateDegreesPerSecond()) <= 1.0);

	// Shaped command neutral. Read from the processor's LastCommand, which
	// FVehicleInputProcessor::ResetState() default-constructs.
	//
	// HONEST SCOPE NOTE: the three HELD flags below are zero here because this test
	// releases the controls at the same moment it resets. They are NOT cleared by
	// NotifyVehicleReset() -- it deliberately preserves PendingSample's bShiftUpHeld,
	// bShiftDownHeld and bResetHeld (VEH-005 MEDIUM-1), because a reset does not lift
	// the driver's fingers off the pad and clearing them manufactures a phantom shift
	// edge or re-arms the reset hold. Only NotifyUnpossessed() clears them. So this
	// assertion covers the SMOOTHED AXES, which are the half that must not survive.
	const FVehicleInputCommand PostResetCommand = Fixture.GetLastCommand();
	TestTrue(
		FString::Printf(TEXT("The shaped command is neutral after the reset: throttle %.4f, brake %.4f, steer %.4f"),
			PostResetCommand.Throttle, PostResetCommand.Brake, PostResetCommand.Steer),
		FMath::IsNearlyZero(PostResetCommand.Throttle)
			&& FMath::IsNearlyZero(PostResetCommand.Brake)
			&& FMath::IsNearlyZero(PostResetCommand.Steer));

	// ------------------------------------------------------------------
	// AND THE FRAME AFTER. This is where the detector actually runs.
	// ------------------------------------------------------------------
	//
	// Every assertion above reads game-thread state the instant ExecuteSafeReset
	// returned, before any capture. The failure detector's verdict on the reset does
	// not exist yet at that point -- it is produced by the NEXT capture, which compares
	// the post-teleport snapshot against the pre-teleport one. Driving on with neutral
	// input is therefore not a tidy-up step; it is the assertion.
	Fixture.Drive(FVehicleInputRawSample(), 10, StepSeconds);

	const FVehicleFailureReport& PostResetReport = Pawn->GetLastFailureReport();
	TestFalse(
		FString::Printf(TEXT("The failure detector stays silent across an announced reset, got [%s]: %s"),
			*RacingSim::Vehicle::DescribeVehicleFailureFlags(PostResetReport.Flags), *PostResetReport.Reason),
		PostResetReport.HasAnyFailure());

	// Capture is still running. Without this, "no failure" is indistinguishable from
	// "the telemetry pipeline stopped at the reset", which NotifyTelemetryDiscontinuity
	// is one line away from being able to cause: it sets NextCaptureTimeSeconds = 0.
	const FVehicleTelemetrySnapshot PostResetSnapshot = Pawn->GetLastTelemetrySnapshot();
	TestTrue(TEXT("Telemetry capture resumed after the reset"), PostResetSnapshot.bIsValid);

	// Still where it was put, plus settling. The car must not drift, sink through the
	// slab or be flung by a residual impulse over the ten steps above.
	//
	// Unlike the null-track no-op further down, this case DOES tick, so it needs a real
	// allowance -- but one with arithmetic behind it. ExecuteSafeReset zeroes both
	// velocities (RacingVehiclePawn.cpp:1030-1031) and leaves the car at tyre-touch
	// height, so across ten steps of 1/60 s = 0.167 s the largest displacement physics
	// can produce is the unresisted fall 0.5 * 980 * 0.167^2 = 13.6 cm, and the springs
	// oppose even that. 30 cm is a bit over twice the worst case: loose enough not to
	// flake on settling, tight enough that the 100 cm it replaces -- 6 m/s of drift, and
	// wider than the car is long -- can no longer pass as "stays at its reset pose".
	constexpr double SettledDisplacementToleranceCm = 30.0;
	TestTrue(
		FString::Printf(TEXT("The car stays at its reset pose: moved %.2f cm in 10 neutral steps"),
			FVector::Dist(Pawn->GetActorLocation(), PostResetLocation)),
		FVector::Dist(Pawn->GetActorLocation(), PostResetLocation) <= SettledDisplacementToleranceCm);

	// Split out because the two directions have different worst cases and lumping them
	// together hides the interesting one: settling is VERTICAL, so any horizontal travel
	// at all is drift or a residual impulse, and neutral input from zeroed velocity
	// should produce essentially none.
	constexpr double SettledHorizontalToleranceCm = 5.0;
	TestTrue(
		FString::Printf(TEXT("The car does not drift sideways while settling: %.2f cm horizontally"),
			FVector::Dist2D(Pawn->GetActorLocation(), PostResetLocation)),
		FVector::Dist2D(Pawn->GetActorLocation(), PostResetLocation) <= SettledHorizontalToleranceCm);

	return true;
}

/**
 * The documented no-op: a null track must leave the car exactly where it is.
 *
 * This is not a formality either. ExecuteSafeReset's own comment records why the guard
 * exists -- without it, GetResetPoseAtOrBeforeDistanceCm returns FTransform::Identity
 * and the car is teleported to the world origin. "Reset with no track silently moves
 * the car to (0,0,0)" is a bug that looks exactly like a working reset from inside the
 * function, and only a caller-side assertion can tell the difference.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVehicleSafeResetWithoutTrackTest,
	"RacingSim.Vehicle.Manoeuvre.SafeResetWithoutTrackIsANoOp",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::ProductFilter)

bool FVehicleSafeResetWithoutTrackTest::RunTest(const FString& Parameters)
{
	using namespace VehicleResetUnderLoadPrivate;

	// The no-op path logs at Warning severity, which the automation framework escalates
	// to a test failure unless it is declared. Declaring it is the assertion that the
	// no-op ANNOUNCED itself: a silent no-op would leave a driver's reset request
	// apparently ignored with nothing in the log to explain it.
	AddExpectedError(TEXT("ExecuteSafeReset called with a null/invalid Track"),
		EAutomationExpectedErrorFlags::Contains, /*Occurrences*/ 1);

	FVehicleManoeuvreFixture Fixture;
	if (!Fixture.Setup(*this, /*bEnableTelemetry*/ false))
	{
		return false;
	}

	ARacingVehiclePawn* Pawn = Fixture.GetPawn();
	if (Pawn == nullptr)
	{
		return false;
	}

	// Move the car somewhere unambiguous first, so "did not move" cannot be satisfied
	// by a car that was already sitting at the origin the bug would teleport it to.
	Fixture.Drive(FVehicleManoeuvreFixture::ThrottleSample(1.0, 0.0), 120, StepSeconds);

	const FVector BeforeLocation = Pawn->GetActorLocation();
	TestTrue(
		FString::Printf(TEXT("The car has driven clear of the origin before the no-op: %.2f cm away"),
			BeforeLocation.Size2D()),
		BeforeLocation.Size2D() > 500.0);

	Pawn->ExecuteSafeReset(/*Track*/ nullptr, /*LapTracker*/ nullptr, RequestedProgressCm);

	// EXACT, not approximate. No world tick happens between BeforeLocation and this
	// line -- ExecuteSafeReset returns without touching the transform on the null-track
	// path, and nothing integrates the car's motion in between -- so the only correct
	// displacement is zero, and KINDA_SMALL_NUMBER is float noise rather than a physics
	// allowance. The previous 100 cm bound would have passed a no-op that moved the car
	// most of a car length, which is not a no-op.
	TestTrue(
		FString::Printf(TEXT("A null track leaves the car exactly where it was: moved %.4f cm"),
			FVector::Dist(Pawn->GetActorLocation(), BeforeLocation)),
		FVector::Dist(Pawn->GetActorLocation(), BeforeLocation) <= KINDA_SMALL_NUMBER);

	TestTrue(TEXT("A null-track no-op does not teleport the car to the world origin"),
		Pawn->GetActorLocation().Size2D() > 500.0);

	return true;
}
