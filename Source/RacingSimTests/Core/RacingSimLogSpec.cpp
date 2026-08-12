// Copyright RacingSim. All Rights Reserved.

#include "Core/RacingSimLog.h"
#include "RacingSimTestsLog.h"
#include "Misc/AutomationTest.h"

/**
 * CORE-001 smoke test.
 *
 * Deliberately narrow. There is no gameplay yet, so this asserts the one thing
 * CORE-001 actually delivers: the test module links against the runtime module, and
 * every architecture layer has a distinct, registered logging category.
 *
 * It is a real assertion rather than a placeholder. If a future change drops a
 * category, renames one, or accidentally points two layers at the same category, this
 * fails. That last case is the one worth catching -- copy-pasting a DEFINE_LOG_CATEGORY
 * line and forgetting to change the name produces code that compiles and logs to the
 * wrong layer.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRacingSimLogCategoriesTest,
	"RacingSim.Core.LogCategories",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		// SmokeFilter, not ProductFilter. The project's recorded automation command
		// runs "Automation RunFilter Smoke", so a Product-filtered test would pass
		// review, sit in the tree, and never execute under the documented gate --
		// counted as coverage by every later ticket without ever running. The test
		// completes in ~11 ms, which is what Smoke is for.
		| EAutomationTestFlags::SmokeFilter)

bool FRacingSimLogCategoriesTest::RunTest(const FString& Parameters)
{
	const TArray<FName> ExpectedNames = {
		LogRacingCore.GetCategoryName(),
		LogRacingVehicle.GetCategoryName(),
		LogRacingRace.GetCategoryName(),
		LogRacingUI.GetCategoryName(),
		LogRacingStreaming.GetCategoryName()
	};

	TestEqual(TEXT("One category per architecture layer"), ExpectedNames.Num(), 5);

	// Distinctness: a duplicated DEFINE_LOG_CATEGORY name compiles fine and silently
	// merges two layers' output. Catch it here.
	TSet<FName> UniqueNames(ExpectedNames);
	TestEqual(TEXT("Every layer category is distinct"), UniqueNames.Num(), ExpectedNames.Num());

	for (const FName& Name : ExpectedNames)
	{
		TestFalse(
			FString::Printf(TEXT("Category name is not None: %s"), *Name.ToString()),
			Name.IsNone());
	}

	// Verify the names are the ones the architecture documents refer to, so a rename
	// has to be a deliberate act that updates this list too.
	TestEqual(TEXT("Core category name"), LogRacingCore.GetCategoryName(), FName("LogRacingCore"));
	TestEqual(TEXT("Vehicle category name"), LogRacingVehicle.GetCategoryName(), FName("LogRacingVehicle"));
	TestEqual(TEXT("Race category name"), LogRacingRace.GetCategoryName(), FName("LogRacingRace"));
	TestEqual(TEXT("UI category name"), LogRacingUI.GetCategoryName(), FName("LogRacingUI"));
	TestEqual(TEXT("Streaming category name"), LogRacingStreaming.GetCategoryName(), FName("LogRacingStreaming"));

	// The test module has its own category so harness diagnostics are distinguishable
	// from the output of the code under test. It must not collide with a layer.
	TestEqual(TEXT("Tests category name"), LogRacingTests.GetCategoryName(), FName("LogRacingTests"));
	TestFalse(
		TEXT("Test module category is not one of the layer categories"),
		UniqueNames.Contains(LogRacingTests.GetCategoryName()));

	return true;
}
