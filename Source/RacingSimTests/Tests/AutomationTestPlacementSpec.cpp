// Copyright RacingSim. All Rights Reserved.

#include "RacingSimTestsLog.h"

#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"

/**
 * TEST-001, closing CORE-001 finding N-2 at runtime.
 *
 * Two assertions, in decreasing order of strength:
 *
 *   1. No registered automation test -- whatever it is named -- is defined in a source
 *      file under Source/RacingSim/. Filtered by source location, not by test name,
 *      because the name is author-chosen: a rogue test registered as "MyGame.Foo" or
 *      "Scratch.Temp" inside the runtime module is precisely the case a name filter
 *      cannot see. (TEST-001 repair cycle 1, finding T-4.)
 *   2. Every test named "RacingSim.*" is defined under Source/RacingSimTests/. Scoped
 *      by name because engine and plugin tests legitimately live elsewhere.
 *
 * A test declared inside Source/RacingSim/ compiles into a packaged Development Game
 * target (WITH_DEV_AUTOMATION_TESTS is 1 there), and none of CORE-001's checks would
 * notice: the .target receipt and the staging manifest both assert which *modules* were
 * built, and RacingSim is supposed to be built.
 *
 * This is the second of two layers. The first is a source-text scan in
 * RacingSim.Build.cs that fails the build. They are deliberately different in kind:
 *
 *   - the build scan sees any banned macro, including in a file whose test would never
 *     register in this context, but cannot see a test registered without a macro;
 *   - this test sees the framework's own registry, including hand-rolled
 *     registrations, but only for tests whose flags make them visible in the current
 *     application context.
 *
 * Known blind spot, stated rather than papered over: FAutomationTestFramework::
 * GetValidTestNames filters by application context (AutomationTest.cpp:800-845), so a
 * rogue test flagged ClientContext-only would be invisible *here* while running under
 * a commandlet. That case is the build scan's job, and it is why both layers exist.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRacingSimAutomationTestPlacementTest,
	"RacingSim.Tests.AutomationTestPlacement",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		// SmokeFilter for the reason recorded in Docs/Environment.md: the project's only
		// documented automation command is "Automation RunFilter Smoke", so a test on
		// any other filter would sit in the tree and never run under the documented gate.
		| EAutomationTestFlags::SmokeFilter)

bool FRacingSimAutomationTestPlacementTest::RunTest(const FString& Parameters)
{
	// The prefix this project's own tests use. Used only for the *second*, narrower
	// assertion (project tests live in the test module). It is deliberately NOT the
	// filter for the primary assertion -- see below.
	static const FString ProjectTestPrefix(TEXT("RacingSim."));

	// The only directory a project test may be defined in. Compared with forward
	// slashes because __FILE__ on MSVC yields the path as it appeared on the compiler
	// command line, which is backslashed on Windows.
	//
	// Note "/Source/RacingSim/" does not match "/Source/RacingSimTests/": the trailing
	// slash after RacingSim makes the two mutually exclusive, so the forbidden fragment
	// cannot fire on a correctly placed test.
	static const FString PermittedSourceFragment(TEXT("/Source/RacingSimTests/"));
	static const FString ForbiddenSourceFragment(TEXT("/Source/RacingSim/"));

	TArray<FAutomationTestInfo> AllTests;
	FAutomationTestFramework::Get().GetValidTestNames(AllTests);

	// Positive control. If the framework returned nothing, or returned tests but none
	// of this project's, every assertion below would vacuously pass and the test would
	// report a green result while checking nothing. CORE-001's artifact check made the
	// same mistake available and guarded against it the same way; keep the guard.
	TestTrue(
		TEXT("Automation framework returned a non-empty test list"),
		AllTests.Num() > 0);

	int32 ProjectTestCount = 0;
	int32 SourceFileKnownCount = 0;
	int32 PermittedModuleCount = 0;

	for (const FAutomationTestInfo& Info : AllTests)
	{
		const FString FullPath = Info.GetFullTestPath();

		FString SourceFile = Info.GetSourceFile();
		FPaths::NormalizeFilename(SourceFile);

		if (!SourceFile.IsEmpty())
		{
			++SourceFileKnownCount;
		}

		if (SourceFile.Contains(PermittedSourceFragment, ESearchCase::IgnoreCase))
		{
			++PermittedModuleCount;
		}

		// -------------------------------------------------------------------
		// Primary assertion, filtered by SOURCE LOCATION and not by test name.
		// -------------------------------------------------------------------
		//
		// The earlier version of this test filtered on the "RacingSim." name prefix
		// first and only then looked at the source file. That made the check blind to
		// exactly the thing it exists to catch: a test declared inside
		// Source/RacingSim/ under any other registered name -- "MyGame.Foo",
		// "Scratch.Temp", a copied engine test name -- would be skipped by the prefix
		// filter and ship inside the runtime module unnoticed. The registered name is
		// author-chosen and therefore worthless as a safety filter; the source path is
		// the property the rule is actually about.
		//
		// So: every registered test, whatever it is called, must not come from the
		// runtime module.
		TestFalse(
			FString::Printf(
				TEXT("Registered test '%s' is NOT defined in the runtime module ")
				TEXT("Source/RacingSim/ (it would compile into a packaged Game target; ")
				TEXT("source: %s)"),
				*FullPath,
				*SourceFile),
			SourceFile.Contains(ForbiddenSourceFragment, ESearchCase::IgnoreCase));

		// -------------------------------------------------------------------
		// Secondary assertion, scoped to this project's own tests.
		// -------------------------------------------------------------------
		//
		// Engine and plugin tests legitimately live outside both modules, so "must be
		// under Source/RacingSimTests/" can only be required of tests this project
		// owns. Ownership is claimed by the name prefix here, which is fine for this
		// direction: a rogue test that lies about its name is already caught above.
		if (!FullPath.StartsWith(ProjectTestPrefix, ESearchCase::CaseSensitive))
		{
			continue;
		}

		++ProjectTestCount;

		// A project test with no recorded source file cannot be placed, so it cannot be
		// proven to live in the test module. Treat that as a failure rather than
		// skipping it -- skipping is how a check quietly stops checking.
		TestFalse(
			FString::Printf(TEXT("Project test reports a source file: %s"), *FullPath),
			SourceFile.IsEmpty());

		TestTrue(
			FString::Printf(
				TEXT("Test '%s' is defined under Source/RacingSimTests/ (source: %s)"),
				*FullPath,
				*SourceFile),
			SourceFile.Contains(PermittedSourceFragment, ESearchCase::IgnoreCase));
	}

	// Second half of the positive control: this test is itself a RacingSim.* test, so
	// the count can never legitimately be zero. Zero means the enumeration or the
	// prefix broke, not that the project is clean.
	TestTrue(
		FString::Printf(
			TEXT("At least one RacingSim.* test was enumerated (found %d)"),
			ProjectTestCount),
		ProjectTestCount > 0);

	// Control specific to the inverted (primary) assertion above, and the one that
	// keeps it honest. That assertion is a TestFalse on a substring of GetSourceFile().
	// If GetSourceFile() returned an empty string for every test -- a framework change,
	// a build without __FILE__ recorded, a packaging mode that strips it -- the
	// assertion would pass for every test in the project while inspecting nothing, and
	// this spec would report green forever. Prove the source path is actually populated
	// and actually discriminates, by requiring that at least one test is seen to come
	// from Source/RacingSimTests/ (this spec, at minimum).
	TestTrue(
		FString::Printf(
			TEXT("Source paths are populated: %d of %d registered tests report one"),
			SourceFileKnownCount,
			AllTests.Num()),
		SourceFileKnownCount > 0);

	TestTrue(
		FString::Printf(
			TEXT("The source-path matcher discriminates: %d registered test(s) matched ")
			TEXT("Source/RacingSimTests/ (this spec is one of them)"),
			PermittedModuleCount),
		PermittedModuleCount > 0);

	UE_LOG(
		LogRacingTests,
		Log,
		TEXT("AutomationTestPlacement: %d registered tests scanned by source path; %d ")
		TEXT("reported a source file; %d from Source/RacingSimTests/; %d RacingSim.* ")
		TEXT("tests checked for placement."),
		AllTests.Num(),
		SourceFileKnownCount,
		PermittedModuleCount,
		ProjectTestCount);

	return true;
}
