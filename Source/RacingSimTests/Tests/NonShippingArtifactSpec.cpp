// Copyright RacingSim. All Rights Reserved.

#include "RacingSimTestsLog.h"

#include "Misc/AutomationTest.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

/**
 * TEST-001, closing CORE-001 finding N-4 inside the documented automation gate.
 *
 * Scripts/Test/Check-NonShippingArtifacts.ps1 is the full check and is the one to run
 * around a package. This test exists because the project's *documented* gate is
 * "Automation RunFilter Smoke" (Docs/Environment.md), and a check that lives only in a
 * script nobody is required to run is the same failure mode as a test on a filter the
 * recorded command never uses -- which is a mistake this project already made once and
 * corrected at CORE-001.
 *
 * What it asserts, and how each one can fail:
 *
 *  1. RacingSimEditor.target lists RacingSimTests. Fails if the test module stops
 *     building, or if the receipt's shape changes so this check can no longer read it.
 *     This is the positive control for (2): without it, a parser that always returned
 *     an empty module list would make (2) pass forever.
 *
 *  2. RacingSim.target (Game) does not list RacingSimTests. Fails if someone adds the
 *     test module to RacingSim.Target.cs, or sets bBuildRequiresCookedDataOverride =
 *     false on the Game target. CORE-001's reviewer named those two edits precisely and
 *     noted that today "no build fails and no test fails" when they happen. After this,
 *     one does.
 *
 *  3. DirectoriesToNeverCook still contains the test-content paths. Fails if a later
 *     ticket rewrites Config/DefaultGame.ini and drops the packaging section.
 *
 * Deliberate asymmetry: a missing *Editor* receipt is an error, because this test only
 * runs from a built editor, so the receipt must exist. A missing *Game* receipt is a
 * warning, because building the Game target is not a precondition for running the
 * editor's automation suite, and turning "you have not built the Game target on this
 * machine yet" into a red test would train people to ignore it. The script covers the
 * package-time path and fails hard there.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRacingSimNonShippingArtifactTest,
	"RacingSim.Tests.NonShippingArtifacts",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

namespace RacingSimNonShippingArtifactSpec
{
	/** Absolute path to a UnrealBuildTool .target receipt for the given target name. */
	static FString ReceiptPath(const TCHAR* TargetName)
	{
		return FPaths::ConvertRelativePathToFull(
			FPaths::Combine(
				FPaths::ProjectDir(),
				TEXT("Binaries"),
				TEXT("Win64"),
				FString::Printf(TEXT("%s.target"), TargetName)));
	}
}

bool FRacingSimNonShippingArtifactTest::RunTest(const FString& Parameters)
{
	using namespace RacingSimNonShippingArtifactSpec;

	static const TCHAR* TestModuleName = TEXT("RacingSimTests");
	static const TCHAR* RuntimeModuleName = TEXT("RacingSim");

	// -----------------------------------------------------------------------
	// 1 + 2. .target receipts.
	// -----------------------------------------------------------------------
	//
	// Substring search on the raw JSON rather than a parse. The receipt names modules
	// in more than one place (the Modules map, and Additional/BuildProducts entries),
	// and the question here is the coarse one -- does UBT mention this module at all
	// for this target. A parse would be more precise and more brittle; the PowerShell
	// script does the parse, and prints both numbers, so the two disagree loudly if the
	// format ever changes.

	const FString EditorReceipt = ReceiptPath(TEXT("RacingSimEditor"));
	const FString GameReceipt = ReceiptPath(TEXT("RacingSim"));

	FString EditorReceiptText;
	const bool bEditorReceiptRead = FFileHelper::LoadFileToString(EditorReceiptText, *EditorReceipt);

	if (TestTrue(
			FString::Printf(TEXT("Editor .target receipt is readable: %s"), *EditorReceipt),
			bEditorReceiptRead))
	{
		// Positive control. If this fails, assertion (2) proves nothing.
		TestTrue(
			TEXT("RacingSimEditor.target lists RacingSimTests (control: the search works "
				 "and the test module is really built for the editor)"),
			EditorReceiptText.Contains(TestModuleName, ESearchCase::CaseSensitive));

		TestTrue(
			TEXT("RacingSimEditor.target lists RacingSim (control)"),
			EditorReceiptText.Contains(RuntimeModuleName, ESearchCase::CaseSensitive));
	}

	FString GameReceiptText;
	if (FFileHelper::LoadFileToString(GameReceiptText, *GameReceipt))
	{
		// Control first: prove the Game receipt was actually read and is non-trivial,
		// so "RacingSimTests not found" means absence rather than an empty string.
		TestTrue(
			TEXT("RacingSim.target lists RacingSim (control)"),
			GameReceiptText.Contains(RuntimeModuleName, ESearchCase::CaseSensitive));

		TestFalse(
			TEXT("RacingSim.target (Game) does NOT list RacingSimTests -- the UncookedOnly "
				 "test module was not compiled into the Game target"),
			GameReceiptText.Contains(TestModuleName, ESearchCase::CaseSensitive));

		UE_LOG(LogRacingTests, Log,
			TEXT("NonShippingArtifacts: checked Game receipt %s (%d chars)."),
			*GameReceipt, GameReceiptText.Len());
	}
	else
	{
		// Not an error. See the class comment for why this is deliberately a warning.
		AddWarning(FString::Printf(
			TEXT("Game .target receipt not present at %s, so the Game-target half of the ")
			TEXT("non-shipping check did not run. Build 'RacingSim Win64 Development', or ")
			TEXT("run Scripts/Test/Check-NonShippingArtifacts.ps1 -Mode Receipt, which fails ")
			TEXT("hard on a missing receipt."),
			*GameReceipt));
	}

	// -----------------------------------------------------------------------
	// 3. Test content is configured never to cook.
	// -----------------------------------------------------------------------
	//
	// Read through GConfig rather than by reading DefaultGame.ini as text. That is the
	// stronger statement: it asserts the value survives the whole config hierarchy
	// (Base -> Default -> platform -> command-line overrides) as the cooker will see
	// it, not merely that a line exists in a file. UCookOnTheFlyServer reads the same
	// property at CookOnTheFlyServer.cpp:12463.
	static const TCHAR* PackagingSection = TEXT("/Script/UnrealEd.ProjectPackagingSettings");

	TArray<FString> NeverCookDirectories;
	GConfig->GetArray(PackagingSection, TEXT("DirectoriesToNeverCook"), NeverCookDirectories, GGameIni);

	// Positive control on the config read itself.
	TestTrue(
		FString::Printf(
			TEXT("DirectoriesToNeverCook resolves to a non-empty array through GConfig (%d entries)"),
			NeverCookDirectories.Num()),
		NeverCookDirectories.Num() > 0);

	const TArray<FString> RequiredNeverCookPaths = { TEXT("/Game/Tests"), TEXT("/Game/Developer") };

	for (const FString& RequiredPath : RequiredNeverCookPaths)
	{
		const bool bFound = NeverCookDirectories.ContainsByPredicate(
			[&RequiredPath](const FString& Entry)
			{
				// Entries arrive as struct literals, e.g. (Path="/Game/Tests"). Match on
				// the quoted value so a path that is merely a prefix of another --
				// /Game/Test vs /Game/Tests -- cannot satisfy this.
				return Entry.Contains(
					FString::Printf(TEXT("Path=\"%s\""), *RequiredPath),
					ESearchCase::CaseSensitive);
			});

		TestTrue(
			FString::Printf(
				TEXT("Config/DefaultGame.ini excludes %s from cooking (DirectoriesToNeverCook)"),
				*RequiredPath),
			bFound);
	}

	// Negative control for the matcher above.
	const bool bBogusFound = NeverCookDirectories.ContainsByPredicate(
		[](const FString& Entry)
		{
			return Entry.Contains(TEXT("Path=\"/Game/NotConfigured\""), ESearchCase::CaseSensitive);
		});

	TestFalse(
		TEXT("An unconfigured path is not reported as excluded (matcher is sound)"),
		bBogusFound);

	return true;
}
