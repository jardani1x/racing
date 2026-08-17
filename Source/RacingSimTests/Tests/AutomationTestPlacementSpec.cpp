// Copyright RacingSim. All Rights Reserved.

#include "RacingSimTestsLog.h"

#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"

/**
 * TEST-001, closing CORE-001 finding N-2 at runtime.
 *
 * Asserts that every automation test this project owns is defined in a source file
 * under Source/RacingSimTests/. A test declared inside Source/RacingSim/ compiles into
 * a packaged Development Game target (WITH_DEV_AUTOMATION_TESTS is 1 there), and none
 * of CORE-001's checks would notice: the .target receipt and the staging manifest both
 * assert which *modules* were built, and RacingSim is supposed to be built.
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
	// The prefix every test this project owns must use. Engine tests are not this
	// project's business and are skipped.
	static const FString ProjectTestPrefix(TEXT("RacingSim."));

	// The only directory a project test may be defined in. Compared with forward
	// slashes because __FILE__ on MSVC yields the path as it appeared on the compiler
	// command line, which is backslashed on Windows.
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

	for (const FAutomationTestInfo& Info : AllTests)
	{
		const FString FullPath = Info.GetFullTestPath();
		if (!FullPath.StartsWith(ProjectTestPrefix, ESearchCase::CaseSensitive))
		{
			continue;
		}

		++ProjectTestCount;

		FString SourceFile = Info.GetSourceFile();
		FPaths::NormalizeFilename(SourceFile);

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

		// Stated separately from the assertion above so the failure message names the
		// actual defect -- "this test ships inside the runtime module" -- rather than
		// the generic "not in the expected folder".
		TestFalse(
			FString::Printf(
				TEXT("Test '%s' is NOT defined in the runtime module Source/RacingSim/ ")
				TEXT("(it would compile into a packaged Game target; source: %s)"),
				*FullPath,
				*SourceFile),
			SourceFile.Contains(ForbiddenSourceFragment, ESearchCase::IgnoreCase)
				&& !SourceFile.Contains(PermittedSourceFragment, ESearchCase::IgnoreCase));
	}

	// Second half of the positive control: this test is itself a RacingSim.* test, so
	// the count can never legitimately be zero. Zero means the enumeration or the
	// prefix broke, not that the project is clean.
	TestTrue(
		FString::Printf(
			TEXT("At least one RacingSim.* test was enumerated (found %d)"),
			ProjectTestCount),
		ProjectTestCount > 0);

	UE_LOG(
		LogRacingTests,
		Log,
		TEXT("AutomationTestPlacement: checked %d RacingSim.* tests out of %d registered."),
		ProjectTestCount,
		AllTests.Num());

	return true;
}
