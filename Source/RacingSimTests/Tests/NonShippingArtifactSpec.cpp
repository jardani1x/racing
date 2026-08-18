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
 *  2. RacingSimTests is not among the modules linked into RacingSim.exe, read from the
 *     Game target's LINKER RESPONSE FILE. Fails if someone adds the test module to
 *     RacingSim.Target.cs, or sets bBuildRequiresCookedDataOverride = false on the Game
 *     target. CORE-001's reviewer named those two edits precisely and noted that today
 *     "no build fails and no test fails" when they happen. After this, one does.
 *
 *  3. DirectoriesToNeverCook still contains the test-content paths. Fails if a later
 *     ticket rewrites Config/DefaultGame.ini and drops the packaging section.
 *
 * ---------------------------------------------------------------------------
 * Why (2) reads a .rsp and not Binaries/Win64/RacingSim.target (finding T-1)
 * ---------------------------------------------------------------------------
 *
 * The first version of (2) asserted that the string "RacingSimTests" appears 0 times in
 * the Game .target receipt. That assertion could not fail on its subject, and the claim
 * that it caught the two edits above was false.
 *
 * A .target receipt lists build PRODUCTS, not modules. A Development Game target is
 * monolithic: every module is compiled into one RacingSim.exe, so no module appears as
 * a build product and no module name appears in the receipt. Measured on this
 * repository in the known-good state, RacingSim.target contains 0 occurrences of
 * "RacingSimTests" -- and also 0 of "InputCore", 0 of "CoreUObject" and 0 of
 * "SlateCore", all of which are certainly linked into that exe. The old number
 * described monolithic-vs-modular linkage, not module membership, and would have read 0
 * just the same with the test module compiled in.
 *
 * UBT writes every link input to
 *   Intermediate/Build/Win64/x64/RacingSim/Development/RacingSim.exe.rsp
 * as one .obj per translation unit, under a per-module directory:
 *   ".../Development/InputCore/Module.InputCore.cpp.obj"
 * Measured here: 1122 .obj inputs, all 1122 matching /Development/<Module>/<file>.obj,
 * yielding ~500 module names -- including RacingSim, Core, Engine, InputCore and
 * CoreUObject, and excluding RacingSimTests. That file can distinguish linked from
 * not-linked; the receipt cannot.
 *
 * Both halves degrade to a warning when their input is absent, rather than failing.
 * Neither building the Game target nor retaining intermediates is a precondition for
 * running the editor's automation suite, and turning "you have not built that here yet"
 * into a red test trains people to ignore red tests. Scripts/Test/Check-NonShipping
 * Artifacts.ps1 covers the package-time path and fails hard on a missing input.
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

	/**
	 * Absolute path to the linker response file for the Win64 Development Game target.
	 * This is what the documented build command in Docs/Environment.md produces.
	 */
	static FString GameLinkResponsePath()
	{
		return FPaths::ConvertRelativePathToFull(
			FPaths::Combine(
				FPaths::ProjectDir(),
				TEXT("Intermediate"), TEXT("Build"), TEXT("Win64"), TEXT("x64"),
				TEXT("RacingSim"), TEXT("Development"),
				TEXT("RacingSim.exe.rsp")));
	}

	/**
	 * Distinct module names linked into a monolithic target, taken from the response
	 * file. Each object input looks like
	 *   ".../Intermediate/Build/Win64/x64/UnrealGame/Development/InputCore/Module.InputCore.cpp.obj"
	 * so the module is the directory immediately above the object file. Verified on
	 * this repository: all 1122 .obj inputs match that shape and none contain a
	 * backslash.
	 */
	static TSet<FString> ParseLinkedModules(const FString& ResponseText)
	{
		static const FString Marker(TEXT("/Development/"));

		TSet<FString> Modules;

		int32 SearchStart = 0;
		int32 MarkerIndex = INDEX_NONE;

		while ((MarkerIndex = ResponseText.Find(
					Marker, ESearchCase::IgnoreCase, ESearchDir::FromStart, SearchStart)) != INDEX_NONE)
		{
			const int32 ModuleStart = MarkerIndex + Marker.Len();
			SearchStart = ModuleStart;

			int32 ModuleEnd = INDEX_NONE;
			for (int32 Index = ModuleStart; Index < ResponseText.Len(); ++Index)
			{
				const TCHAR Char = ResponseText[Index];
				if (Char == TEXT('/'))
				{
					ModuleEnd = Index;
					break;
				}
				// A module directory name is a bare identifier. Anything else means
				// this "/Development/" was not part of an object path.
				if (Char != TEXT('_') && !FChar::IsAlnum(Char))
				{
					break;
				}
			}

			if (ModuleEnd == INDEX_NONE || ModuleEnd <= ModuleStart)
			{
				continue;
			}

			// Only count it if what follows is an object file, i.e. this really is a
			// per-module link input and not some other path that happens to contain
			// "/Development/".
			const int32 FileStart = ModuleEnd + 1;
			int32 FileEnd = FileStart;
			while (FileEnd < ResponseText.Len()
				&& ResponseText[FileEnd] != TEXT('/')
				&& ResponseText[FileEnd] != TEXT('"'))
			{
				++FileEnd;
			}

			const FString FileName = ResponseText.Mid(FileStart, FileEnd - FileStart);
			if (FileName.EndsWith(TEXT(".obj"), ESearchCase::IgnoreCase))
			{
				Modules.Add(ResponseText.Mid(ModuleStart, ModuleEnd - ModuleStart));
			}
		}

		return Modules;
	}
}

bool FRacingSimNonShippingArtifactTest::RunTest(const FString& Parameters)
{
	using namespace RacingSimNonShippingArtifactSpec;

	static const TCHAR* TestModuleName = TEXT("RacingSimTests");
	static const TCHAR* RuntimeModuleName = TEXT("RacingSim");

	// -----------------------------------------------------------------------
	// 1. Editor .target receipt -- positive control.
	// -----------------------------------------------------------------------
	//
	// Substring search on the raw JSON rather than a parse. The Editor target is
	// modular, so each module really does appear as its own build product, and the
	// question here is the coarse one: is RacingSimTests a real module that really is
	// built somewhere. The PowerShell script does the structured parse, so the two
	// disagree loudly if the format ever changes.

	const FString EditorReceipt = ReceiptPath(TEXT("RacingSimEditor"));

	FString EditorReceiptText;
	if (FFileHelper::LoadFileToString(EditorReceiptText, *EditorReceipt))
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
	else
	{
		// T-5: degrade rather than hard-fail. A missing Editor receipt is an
		// environmental condition, not a defect in the thing under test -- a fresh
		// checkout, a different platform layout, or an installed-engine/prebuilt
		// scenario where the editor binaries were not produced by this tree. The
		// Game-side half below is independent of this and still runs. The script
		// fails hard on the same input, which is the right behaviour there because
		// it is run deliberately, at package time.
		AddWarning(FString::Printf(
			TEXT("Editor .target receipt not present at %s, so the control half of the ")
			TEXT("non-shipping check did not run. Build 'RacingSimEditor Win64 ")
			TEXT("Development', or run Scripts/Test/Check-NonShippingArtifacts.ps1 ")
			TEXT("-Mode Receipt, which fails hard on a missing receipt."),
			*EditorReceipt));
	}

	// -----------------------------------------------------------------------
	// 2. Game link inputs -- the assertion the ticket is about.
	// -----------------------------------------------------------------------
	//
	// Read from the linker response file, NOT the Game .target receipt. See the long
	// comment on this class (finding T-1) for why the receipt cannot answer this.

	const FString GameLinkRsp = GameLinkResponsePath();

	FString GameLinkText;
	if (FFileHelper::LoadFileToString(GameLinkText, *GameLinkRsp))
	{
		const TSet<FString> LinkedModules = ParseLinkedModules(GameLinkText);

		// Control: the response file parsed into a module list at all. Without this a
		// path or schema change yields an empty set and the assertion below passes
		// vacuously -- which is precisely the defect T-1 was raised about.
		TestTrue(
			FString::Printf(
				TEXT("Game linker response file parsed into a module list (%d modules)"),
				LinkedModules.Num()),
			LinkedModules.Num() > 0);

		// Control: modules known to be linked into the monolithic exe are named. This
		// is the control the .target receipt could never supply -- it reads 0 for all
		// of these. With this passing, RacingSimTests's absence is a real measurement.
		for (const TCHAR* Expected : { TEXT("RacingSim"), TEXT("Core"), TEXT("CoreUObject"),
									   TEXT("Engine"), TEXT("InputCore") })
		{
			TestTrue(
				FString::Printf(
					TEXT("Known-linked module '%s' is named in the Game link inputs (control)"),
					Expected),
				LinkedModules.Contains(Expected));
		}

		// Negative control for the matcher.
		TestFalse(
			TEXT("A module that does not exist is not reported as linked (matcher is sound)"),
			LinkedModules.Contains(TEXT("RacingSimNotARealModule")));

		// THE assertion, in both forms: parsed module membership, and a raw substring
		// search so a module arriving in some other link form is still caught.
		TestFalse(
			TEXT("RacingSimTests is NOT linked into RacingSim.exe -- the UncookedOnly test "
				 "module was not compiled into the Game target"),
			LinkedModules.Contains(TestModuleName));

		TestFalse(
			TEXT("'RacingSimTests' does not appear anywhere in the Game linker response file"),
			GameLinkText.Contains(TestModuleName, ESearchCase::CaseSensitive));

		UE_LOG(LogRacingTests, Log,
			TEXT("NonShippingArtifacts: %d modules linked into RacingSim.exe per %s."),
			LinkedModules.Num(), *GameLinkRsp);
	}
	else
	{
		// Not an error. See the class comment for why this is deliberately a warning.
		AddWarning(FString::Printf(
			TEXT("Game linker response file not present at %s, so the Game-target half of ")
			TEXT("the non-shipping check did not run. Build 'RacingSim Win64 Development', ")
			TEXT("or run Scripts/Test/Check-NonShippingArtifacts.ps1 -Mode Receipt, which ")
			TEXT("fails hard on a missing input."),
			*GameLinkRsp));
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
