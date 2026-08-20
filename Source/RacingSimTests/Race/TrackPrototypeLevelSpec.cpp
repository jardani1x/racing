// Copyright RacingSim. All Rights Reserved.

#include "Race/TrackDefinitionActor.h"
#include "Race/TrackCenterline.h"
#include "Race/TrackCheckpointGate.h"
#include "Core/RacingSimBuildId.h"

#include "Engine/Level.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

/**
 * TRACK-002: the PLACED track, loaded from the graybox level.
 *
 * ===========================================================================
 * What this file is for, and why it is not more of TrackDefinitionActorSpec
 * ===========================================================================
 *
 * Every other track test in this project runs against a procedurally built fixture --
 * TRACK-001 made that a design goal so the cheap, certain work would not block on
 * level authoring. The consequence is that ONE code path had never executed anywhere
 * in the repository: ATrackDefinitionActor::PostLoad(), because until TRACK-002 there
 * was no placed instance to load. That is TRACK-001 review finding M5, and closing it
 * is what this file exists to do.
 *
 * M5, in full: "PostLoad() calls RebuildTrackData(), which reads USplineComponent
 * geometry, but component PostLoad ordering relative to the owning actor's is not
 * guaranteed by the engine. With no placed instance in the repo, the load path has
 * never run."
 *
 * ===========================================================================
 * Why a bare LoadPackage, and why the attempt COUNT is the real assertion
 * ===========================================================================
 *
 * The level could be opened through the editor's map-loading machinery, but that would
 * destroy the evidence. Opening a map registers components, may re-run construction
 * scripts, and (in a game world) reaches BeginPlay -- and ATrackDefinitionActor rebakes
 * in OnConstruction and BeginPlay as well as PostLoad. A track that loaded, failed to
 * bake in PostLoad, and was silently rescued by OnConstruction would look identical to
 * a track that baked correctly in PostLoad. The test would pass and M5 would still be
 * open, which is worse than not testing it.
 *
 * So this loads the map package and nothing else -- no AutomationEditorCommonUtils::
 * LoadMap, no FEditorFileUtils.
 *
 * THAT IS STILL NOT ENOUGH ON ITS OWN, and finding out why is what this test was for.
 * It was first written to assert World->IsInitialized() == false and
 * GetBakeAttemptCount() == 1, on the reasoning that a bare LoadPackage does not
 * initialise a world or run construction scripts. BOTH ASSERTIONS FAILED WHEN RUN: the
 * editor initialises a world loaded this way, registers its components and re-runs
 * OnConstruction, so the placed track bakes TWICE on the way in.
 *
 * That is not a nuisance to work around; it IS the hazard M5 describes. A PostLoad bake
 * that failed -- because USplineComponent's own PostLoad had not run yet, which the
 * engine does not guarantee -- would be silently repaired by the OnConstruction bake
 * microseconds later, leaving a perfectly healthy track and no trace of the ordering
 * bug. Every observable an outside test can reach (IsTrackDataBuilt, the centerline, the
 * gates, Validate) reports the SECOND bake's result.
 *
 * ATrackDefinitionActor therefore latches its load-time bake's own outcome, in PostLoad,
 * at the only moment it is knowable: DidPostLoadBakeSucceed(). It is a latched fact and
 * nothing repairs it, so a broken load order fails this test permanently rather than
 * being papered over. GetPostLoadBakeAttemptIndex() == 1 pins that the latched result
 * describes the FIRST bake, so it cannot be confused with some other hook's.
 *
 * ===========================================================================
 * This is the project's first non-CDO actor fixture. Do not mutate it.
 * ===========================================================================
 *
 * TEST-001 carries a finding that this project has no working way to instantiate an
 * Actor in automation: NewObject<AActor>(GetTransientPackage()) dies in
 * CreateDefaultSubobject on a TypedElementRegistry assertion, and
 * UWorld::CreateWorld(EWorldType::Game) dies with an access violation inside CreateWorld
 * itself. Both crash the whole run and produce no index.json. TrackDefinitionActorSpec
 * therefore mutates the class default object, process-wide.
 *
 * A loaded package sidesteps both: the actor was created by the engine's own
 * serialisation path, so it is a REAL instance with real subobjects, and no world was
 * needed to get one. See Docs/Environment.md.
 *
 * The price is that the package stays RESIDENT after the first load. Every test in this
 * file gets the same objects, and a mutation would leak into every later test and into
 * any other suite that loads the same map. So: THESE TESTS ARE READ-ONLY. Nothing here
 * writes to the loaded actor, its components, or its package. A future test that needs
 * to mutate a placed actor must duplicate it first, and must say so.
 *
 * (Validate() is const and only calls EnsureTrackDataBuilt(), which is a no-op once the
 * bake has been attempted, so it is a read even though it can bake in principle.)
 *
 * ===========================================================================
 * WHY THESE THREE TESTS ARE ProductFilter WHEN EVERY OTHER SUITE HERE IS SmokeFilter
 * ===========================================================================
 *
 * THIS IS THE ANSWER TO TEST-001's OPEN FINDING, AND IT IS NOT A STYLE CHOICE.
 * Read this before "fixing" the filter back to Smoke for consistency.
 *
 * TRACK-001 recorded that actors cannot be instantiated in this project's automation
 * harness: NewObject<AActor> died on
 *
 *     Assertion failed: RegisteredElementType [TypedElementRegistry.h:536]
 *     Element type 'Components' has not been registered!
 *
 * and UWorld::CreateWorld died inside CreateWorld. Both were recorded as brute facts
 * with no explanation, and the conclusion drawn was "this harness cannot make actors".
 * That conclusion was wrong, and the real cause is a timing one:
 *
 *   1. FEngineLoop::PreInit calls FAutomationTestFramework::Get().RunSmokeTests()
 *      (Runtime/Launch/Private/LaunchEngineLoop.cpp:4376). EVERY SmokeFilter test in
 *      the process runs THERE, during PreInit -- not from the -ExecCmds line, which is
 *      only processed later, on the first engine tick.
 *   2. RegisterEngineElements(), which registers the Object/Actor/Components/SMInstance
 *      typed-element types, is called from UEngine::Init
 *      (Runtime/Engine/Private/UnrealEngine.cpp:2399) -- i.e. AFTER PreInit returns.
 *   3. UActorComponent::PostInitProperties calls
 *      UEngineElementsLibrary::CreateEditorComponentElement for every non-template
 *      component under WITH_EDITOR on the game thread
 *      (Runtime/Engine/Private/Components/ActorComponent.cpp:588), which lands in
 *      UTypedElementRegistry::CreateElementImpl and asserts if the type is unregistered.
 *
 * So a SmokeFilter test runs in a window where creating any non-template actor
 * component is a hard crash of the entire run -- no index.json, no failure report, the
 * gate simply produces nothing. That is exactly what TRACK-001 saw three times, and it
 * is why its fixture had to be the class default object (CDO subobjects are templates,
 * and the element path skips templates).
 *
 * Verified here, not inferred: this file was first written SmokeFilter and crashed the
 * run with that assertion, both under the full `RunFilter Smoke` gate and when invoked
 * alone by name -- the isolation run proving it is a harness-phase problem and not
 * cross-test contamination.
 *
 * ProductFilter tests are NOT run by RunSmokeTests(). They run only from the deferred
 * `Automation RunFilter Product` console command, which executes after UEngine::Init has
 * completed and the element types exist. Same process, same flags, ~15 seconds later in
 * boot, and actors work.
 *
 * THE OBLIGATION THIS CREATES. Docs/Environment.md is emphatic that "a test the
 * documented gate cannot see is not coverage" -- a ProductFilter test is invisible to
 * `RunFilter Smoke`. So TRACK-002 adds a SECOND recorded gate,
 * Scripts/Test/Run-AutomationFilter.ps1 -Filter Product, and Docs/Environment.md now
 * records both. A future ticket that adds an actor-touching test must use ProductFilter
 * AND must confirm the Product gate still runs it; one that does not touch actors should
 * stay SmokeFilter, which is faster and runs earlier.
 *
 * EditorContext is kept for the reason TRACK-001 established with engine-source
 * evidence: the project's gates run with GIsEditor true and IsRunningCommandlet() false,
 * so a CommandletContext-only suite is never collected at all.
 */

namespace TrackPrototypeLevelSpecPrivate
{
	/**
	 * The graybox level authored by Scripts/Content/Author-PrototypeGrayboxLevel.py.
	 *
	 * Hard-coded rather than searched for. A search would let the suite quietly pass
	 * against some other map if this one were renamed or deleted, and "the level exists
	 * at the path the ticket requires" is itself part of what is being asserted.
	 */
	const TCHAR* const MapPackagePath = TEXT("/Game/Tracks/Prototype/Maps/L_Meridian_Graybox");

	/** Id the authoring script stamps on the placed track. Identity is part of the contract. */
	const TCHAR* const ExpectedTrackId = TEXT("Track.Prototype.Meridian");

	/** Gates the authoring script places, including the start/finish gate at distance 0. */
	constexpr int32 ExpectedGateCount = 6;

	/** Sectors the authoring script places. */
	constexpr int32 ExpectedSectorCount = 3;

	/** Docs/03-TrackRaceUI.md's first-circuit brief: 3-5 km of centerline. */
	constexpr double MinTrackLengthCm = 300000.0;
	constexpr double MaxTrackLengthCm = 500000.0;

	/**
	 * The loaded level, plus strong references that keep it alive for the duration of a
	 * test.
	 *
	 * TStrongObjectPtr rather than raw pointers: nothing else roots a package loaded this
	 * way, and a garbage collection triggered by another suite mid-run would otherwise
	 * leave dangling pointers. The guard is scoped to the test, so the package is
	 * collectable again afterwards -- the tests must not depend on it staying loaded, and
	 * must not care if it does.
	 */
	struct FLoadedPrototypeLevel
	{
		TStrongObjectPtr<UPackage> Package;
		TStrongObjectPtr<UWorld> World;
		TStrongObjectPtr<ATrackDefinitionActor> Track;

		/** Number of ATrackDefinitionActor instances found. The level must contain exactly one. */
		int32 TrackActorCount = 0;

		/** Populated on failure; empty on success. */
		FString Error;

		bool IsValid() const { return Error.IsEmpty() && Track.IsValid(); }
	};

	/**
	 * Load the graybox map package WITHOUT initialising a world.
	 *
	 * Deliberately not AutomationEditorCommonUtils::LoadMap or FEditorFileUtils: those
	 * open the map properly, which is exactly what must not happen here. See the file
	 * comment.
	 */
	FLoadedPrototypeLevel LoadPrototypeLevel()
	{
		FLoadedPrototypeLevel Result;

		// Checked before loading so a missing level reports as a missing level rather
		// than as a null package with no explanation.
		if (!FPackageName::DoesPackageExist(MapPackagePath))
		{
			Result.Error = FString::Printf(
				TEXT("Map package '%s' does not exist. It is authored by "
					 "Scripts/Content/Author-PrototypeGrayboxLevel.py; run "
					 "Scripts/Content/Author-PrototypeGrayboxLevel.ps1 to regenerate it."),
				MapPackagePath);
			return Result;
		}

		UPackage* Package = LoadPackage(nullptr, MapPackagePath, LOAD_None);
		if (!Package)
		{
			Result.Error = FString::Printf(TEXT("LoadPackage('%s') returned null."), MapPackagePath);
			return Result;
		}
		Result.Package.Reset(Package);

		UWorld* World = UWorld::FindWorldInPackage(Package);
		if (!World)
		{
			Result.Error = FString::Printf(TEXT("No UWorld inside package '%s'."), MapPackagePath);
			return Result;
		}
		Result.World.Reset(World);

		if (!World->PersistentLevel)
		{
			Result.Error = FString::Printf(TEXT("World in '%s' has no persistent level."), MapPackagePath);
			return Result;
		}

		for (AActor* Actor : World->PersistentLevel->Actors)
		{
			if (ATrackDefinitionActor* Track = Cast<ATrackDefinitionActor>(Actor))
			{
				++Result.TrackActorCount;
				if (!Result.Track.IsValid())
				{
					Result.Track.Reset(Track);
				}
			}
		}

		if (Result.TrackActorCount == 0)
		{
			Result.Error = FString::Printf(
				TEXT("No ATrackDefinitionActor in '%s'."), MapPackagePath);
		}

		return Result;
	}
}

// ===========================================================================
// TRACK-001 M5: the bake runs, correctly, from PostLoad() alone
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTrackPrototypeLevelPostLoadTest,
	"RacingSim.Race.TrackPrototypeLevelPostLoad",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::ProductFilter)

bool FTrackPrototypeLevelPostLoadTest::RunTest(const FString& Parameters)
{
	using namespace TrackPrototypeLevelSpecPrivate;

	const FLoadedPrototypeLevel Level = LoadPrototypeLevel();
	if (!Level.IsValid())
	{
		AddError(Level.Error);
		return false;
	}

	const ATrackDefinitionActor* Track = Level.Track.Get();

	// The acceptance criterion says ONE closed-loop instance. Two placed tracks in one
	// level is not a harmless duplicate: every consumer that finds "the" track by class
	// would pick one arbitrarily, and the two would disagree about lap length.
	TestEqual(TEXT("The level contains exactly one ATrackDefinitionActor"), Level.TrackActorCount, 1);

	// -- What the load actually does, established by running -----------------
	//
	// THE FIRST VERSION OF THIS TEST ASSERTED World->IsInitialized() == false AND
	// GetBakeAttemptCount() == 1, ON THE REASONING THAT A BARE LoadPackage NEITHER
	// INITIALISES A WORLD NOR RUNS CONSTRUCTION SCRIPTS. BOTH ASSERTIONS FAILED, and the
	// failure is the useful part of this test's history rather than a detail to tidy away.
	//
	// Loading a map package in the editor DOES initialise the world, register its
	// components and re-run OnConstruction -- so the placed track is baked TWICE on the
	// way in, and the count is legitimately 2. Had the test kept the count assertion and
	// simply been "corrected" to expect 2, it would have gone green while testing nothing
	// about PostLoad at all: a PostLoad bake that FAILED would still leave a fully baked
	// track behind, because OnConstruction would have repaired it microseconds later.
	//
	// That is exactly the silent-repair hazard M5 is about, so the actor now latches the
	// load-time bake's own outcome (DidPostLoadBakeSucceed), and this test asserts THAT.
	TestFalse(TEXT("The loaded world never began play (BeginPlay's rebuild cannot have run)"),
		Level.World->HasBegunPlay());

	// -- M5 proper ----------------------------------------------------------

	TestTrue(TEXT("A bake was attempted during load"), Track->HasBakeBeenAttempted());

	TestTrue(TEXT("PostLoad() ran a bake on the placed actor"), Track->HasPostLoadBakeRun());

	// Nothing baked this instance before PostLoad did, so the flag below is a statement
	// about the load path and not about whichever hook happened to run first.
	TestEqual(TEXT("PostLoad's bake was the first bake on this instance"),
		Track->GetPostLoadBakeAttemptIndex(), 1);

	// THE ASSERTION THIS FILE EXISTS FOR, AND THE ONE THAT CLOSES TRACK-001 M5.
	// PostLoad reads USplineComponent geometry at a point where the engine does not
	// guarantee the component's own PostLoad has run. If that ordering were unsafe, this
	// is false -- and it stays false, because nothing repairs a latched result.
	TestTrue(TEXT("The bake performed by PostLoad() alone SUCCEEDED (TRACK-001 M5)"),
		Track->DidPostLoadBakeSucceed());

	TestTrue(TEXT("The centerline is built after load"), Track->IsTrackDataBuilt());

	// -- The bake is not merely present, it is correct ----------------------

	FString Reason;
	const bool bValid = Track->Validate(Reason);
	if (!bValid)
	{
		AddError(FString::Printf(TEXT("Placed track failed Validate(): %s"), *Reason));
	}
	TestTrue(TEXT("The placed track validates"), bValid);

	const FTrackCenterline& Centerline = Track->GetCenterline();
	TestTrue(TEXT("The baked centerline is valid"), Centerline.IsValid());
	TestTrue(TEXT("The baked centerline is a closed loop"), Centerline.IsClosedLoop());

	const double LengthCm = Track->GetTrackLengthCm();
	TestTrue(
		FString::Printf(TEXT("Lap length %.1f cm is within Docs/03-TrackRaceUI.md's 3-5 km brief"), LengthCm),
		LengthCm >= MinTrackLengthCm && LengthCm <= MaxTrackLengthCm);

	// The cm->SI boundary on a real placed asset rather than on a fixture, since this is
	// the number a HUD and the design brief are both read off.
	TestNearlyEqual(TEXT("GetTrackLengthMetres() is the centimetre length over 100"),
		Track->GetTrackLengthMetres(), LengthCm / 100.0, 1.0e-6);

	// A bake that produced no samples would still report a length from the spline, so the
	// sample count is checked independently of it.
	TestTrue(TEXT("The bake produced samples"), Track->GetEffectiveSampleCount() > 0);
	TestTrue(TEXT("The bake used a positive step"), Track->GetEffectiveStepCm() > 0.0);

	TestEqual(TEXT("Sector count matches the authored level"), Track->GetNumSectors(), ExpectedSectorCount);
	TestTrue(TEXT("Grid slots were generated"), Track->GetNumGridSlots() > 0);
	TestTrue(TEXT("Reset samples were generated"), Track->GetNumResetSamples() > 0);

	return true;
}

// ===========================================================================
// Identity: a placed track can stamp a result
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTrackPrototypeLevelIdentityTest,
	"RacingSim.Race.TrackPrototypeLevelIdentity",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::ProductFilter)

bool FTrackPrototypeLevelIdentityTest::RunTest(const FString& Parameters)
{
	using namespace TrackPrototypeLevelSpecPrivate;

	const FLoadedPrototypeLevel Level = LoadPrototypeLevel();
	if (!Level.IsValid())
	{
		AddError(Level.Error);
		return false;
	}

	const ATrackDefinitionActor* Track = Level.Track.Get();

	// CORE-002 reserved FRacingSimVersionStamp::TrackVersion with the comment "populated
	// by TRACK-001". Until a track was placed, nothing had ever populated it from a real
	// asset -- only from a fixture. This is that check.
	const FRacingContentVersion Version = Track->GetContentVersion();
	TestTrue(TEXT("The placed track's content version is populated"), Version.IsPopulated());
	TestEqual(TEXT("The placed track carries the authored id"),
		Version.AssetId.ToString(), FString(ExpectedTrackId));
	TestEqual(TEXT("The placed track carries the current schema version"),
		Version.SchemaVersion, ATrackDefinitionActor::TrackSchemaVersion);

	// Hashing twice must agree, or two runs of the same track would report as two
	// different tracks on a leaderboard. Cheap here, and it is the property the whole
	// version stamp rests on.
	TestEqual(TEXT("ComputeContentHash() is stable across calls"),
		Track->ComputeContentHash(), Track->ComputeContentHash());

	// TestTrue rather than TestNotEqual: the hash is uint32, and the TestNotEqual
	// overload set is float/double/string/name plus one template, so a uint32 argument
	// invites an implicit-conversion surprise for no benefit.
	TestTrue(TEXT("The content hash is not the empty-hash sentinel"),
		Track->ComputeContentHash() != 0u);

	return true;
}

// ===========================================================================
// The gates this ticket defines, on the level this ticket authored
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTrackPrototypeLevelGatesTest,
	"RacingSim.Race.TrackPrototypeLevelGates",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::ProductFilter)

bool FTrackPrototypeLevelGatesTest::RunTest(const FString& Parameters)
{
	using namespace TrackPrototypeLevelSpecPrivate;

	const FLoadedPrototypeLevel Level = LoadPrototypeLevel();
	if (!Level.IsValid())
	{
		AddError(Level.Error);
		return false;
	}

	const ATrackDefinitionActor* Track = Level.Track.Get();
	const FRacingCheckpointGateSet& Gates = Track->GetCheckpointGates();

	if (!Gates.IsValid())
	{
		AddError(TEXT("The placed track baked no checkpoint gates."));
		return false;
	}

	TestEqual(TEXT("The level authored the expected number of gates"),
		Gates.NumGates(), ExpectedGateCount);

	const double LengthCm = Track->GetTrackLengthCm();

	// -- The ordering contract, on the placed asset --------------------------

	const FRacingCheckpointGate* StartFinish = Gates.GetGate(FRacingCheckpointGateSet::StartFinishGateIndex);
	if (!StartFinish)
	{
		AddError(TEXT("No start/finish gate on the placed track."));
		return false;
	}

	TestEqual(TEXT("Gate 0 is the start/finish gate"),
		StartFinish->GateId.ToString(), FString(TEXT("Gate.StartFinish")));
	TestNearlyEqual(TEXT("Gate 0 sits exactly on the distance origin"),
		StartFinish->DistanceAlongCm, 0.0, 1.0e-4);
	TestTrue(TEXT("Gate 0 is forward-crossable, or no lap could ever complete"),
		StartFinish->LegalDirection != ERacingGateDirection::Reverse);

	double PreviousDistanceCm = -1.0;
	for (int32 Index = 0; Index < Gates.NumGates(); ++Index)
	{
		const FRacingCheckpointGate* Gate = Gates.GetGate(Index);
		if (!Gate)
		{
			AddError(FString::Printf(TEXT("Gate %d is null on a set reporting %d gates."), Index, Gates.NumGates()));
			return false;
		}

		TestEqual(FString::Printf(TEXT("Gate %d's ordinal matches its index"), Index), Gate->Ordinal, Index);

		TestTrue(FString::Printf(TEXT("Gate %d distance strictly increases (%.1f > %.1f)"),
			Index, Gate->DistanceAlongCm, PreviousDistanceCm),
			Gate->DistanceAlongCm > PreviousDistanceCm);
		PreviousDistanceCm = Gate->DistanceAlongCm;

		TestTrue(FString::Printf(TEXT("Gate %d is inside the lap (%.1f < %.1f)"),
			Index, Gate->DistanceAlongCm, LengthCm),
			Gate->DistanceAlongCm < LengthCm);

		// THE POLYLINE BIAS, ASSERTED ON REAL AUTHORED GEOMETRY. TRACK-001's strongest
		// counter-case is that baked positions sit systematically inside the true curve.
		// FRacingCheckpointGateSet::Build enforces this relationship at bake time; the
		// point of re-checking it here is that this is the first set of gates measured
		// against an authored circuit with real corners rather than a synthetic fixture.
		TestTrue(FString::Printf(
			TEXT("Gate %d's half-width %.1f cm exceeds the placement tolerance %.4f cm"),
			Index, Gate->HalfWidthCm, Gates.GetPlacementToleranceCm()),
			Gate->HalfWidthCm > Gates.GetPlacementToleranceCm());

		// Two gates inside one centerline segment cannot be ordered reliably. The bake
		// rejects that, so on a built set this is a regression guard on the authored
		// spacing rather than on the bake.
		if (Index > 0)
		{
			const FRacingCheckpointGate* Previous = Gates.GetGate(Index - 1);
			TestTrue(FString::Printf(
				TEXT("Gates %d and %d are more than one centerline segment apart"), Index - 1, Index),
				(Gate->DistanceAlongCm - Previous->DistanceAlongCm) > Gates.GetMaxSegmentLengthCm());
		}
	}

	// -- Crossing direction, through the placed actor's own query ------------
	//
	// Deliberately routed through ATrackDefinitionActor::EvaluateGateCrossing rather than
	// the gate set directly: the actor's query is the surface RACE-002 will consume, and
	// this is the only test in the project that exercises it on an actor whose gates came
	// from a saved asset rather than from a fixture.

	{
		// A short motion segment straddling gate 0's plane, on the gate's centre line.
		// Built from the gate's own axes so the test does not assume a world orientation
		// the authored circuit is not obliged to have.
		const FVector Behind = StartFinish->Location - StartFinish->PlaneNormal * 200.0;
		const FVector Ahead = StartFinish->Location + StartFinish->PlaneNormal * 200.0;

		const FRacingGateCrossingResult Forward = Track->EvaluateGateCrossing(0, Behind, Ahead);
		TestTrue(TEXT("A forward crossing of the placed start/finish gate is evaluated"), Forward.bEvaluated);
		TestEqual(TEXT("A forward crossing reports Forward"),
			static_cast<int32>(Forward.Crossing), static_cast<int32>(ERacingGateCrossing::Forward));
		TestTrue(TEXT("A forward crossing matches the gate's legal direction"), Forward.bMatchesLegalDirection);
		TestTrue(TEXT("A forward crossing is through the gate"), Forward.IsThroughGate());

		// CLAUDE.md requires reverse finish crossings to be rejected. It does NOT permit
		// them to be invisible: the reverse case must be distinguishable from "nothing
		// happened", or RACE-002 cannot invalidate the lap and say why.
		const FRacingGateCrossingResult Reverse = Track->EvaluateGateCrossing(0, Ahead, Behind);
		TestTrue(TEXT("A reverse crossing of the placed start/finish gate is evaluated"), Reverse.bEvaluated);
		TestEqual(TEXT("A reverse crossing reports Reverse, not None"),
			static_cast<int32>(Reverse.Crossing), static_cast<int32>(ERacingGateCrossing::Reverse));
		TestFalse(TEXT("A reverse crossing does not match the gate's legal direction"),
			Reverse.bMatchesLegalDirection);

		// Round the outside of the gate: through the plane, outside the rectangle. This is
		// the shortcut signature, and it must not read as a clean crossing.
		const FVector OffsetCm = StartFinish->RightAxis * (StartFinish->HalfWidthCm + 300.0);
		const FRacingGateCrossingResult Wide =
			Track->EvaluateGateCrossing(0, Behind + OffsetCm, Ahead + OffsetCm);
		TestEqual(TEXT("Passing outside the gate's width reports OutsideExtent, not Forward"),
			static_cast<int32>(Wide.Crossing), static_cast<int32>(ERacingGateCrossing::OutsideExtent));
		TestFalse(TEXT("Passing outside the gate's width is not a legal crossing"), Wide.bMatchesLegalDirection);

		// High-speed single-tick crossing on the real asset: 300 km/h is ~8333 cm/s, so a
		// 100 ms hitch is ~833 cm of travel in one evaluation. A volume overlap would step
		// straight over a gate; a segment/plane test cannot.
		const FVector FarBehind = StartFinish->Location - StartFinish->PlaneNormal * 5000.0;
		const FVector FarAhead = StartFinish->Location + StartFinish->PlaneNormal * 5000.0;
		const FRacingGateCrossingResult Fast = Track->EvaluateGateCrossing(0, FarBehind, FarAhead);
		TestEqual(TEXT("A 100 m single-step crossing still reports Forward"),
			static_cast<int32>(Fast.Crossing), static_cast<int32>(ERacingGateCrossing::Forward));

		// And the whole-track query picks the same gate, which is what RACE-002 will call.
		FRacingGateCrossingResult FirstResult;
		const int32 FirstIndex = Track->FindFirstGateCrossing(Behind, Ahead, FirstResult);
		TestEqual(TEXT("FindFirstGateCrossing finds the start/finish gate"), FirstIndex, 0);
		TestTrue(TEXT("FindFirstGateCrossing reports a through-gate crossing"), FirstResult.IsThroughGate());
	}

	return true;
}
