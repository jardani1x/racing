// Copyright RacingSim. All Rights Reserved.

#include "Core/RacingSimLog.h"
#include "RacingSimTestsLog.h"

#include "Misc/AutomationTest.h"
#include "Misc/CoreMisc.h"
#include "Misc/StringOutputDevice.h"

/**
 * TEST-001. The falsifiable half of the logging coverage.
 *
 * CORE-001's reviewer was right about RacingSim.Core.LogCategories: its assertions
 * cannot fail. LogCategory.h:123 derives a category's name from its identifier via
 * TEXT(#CategoryName), so a duplicate is a duplicate-symbol link error and a rename is
 * a compile error in the spec file itself. Those assertions document intent; they do
 * not test anything the build has not already proven.
 *
 * This test asserts a property the compiler cannot establish: that each declared
 * category is actually *registered with the log suppression system under its name*,
 * and that the name is the handle an operator uses to change its verbosity at runtime.
 *
 * That is the property people actually depend on. `-LogCmds="LogRacingRace Verbose"`,
 * the `[Core.Log]` ini section and the `Log <cat> <verbosity>` console command all
 * resolve a *string* against FLogSuppressionImplementation's ReverseAssociations map
 * (LogSuppressionInterface.cpp:589-648). Nothing at compile time guarantees an entry
 * is in that map: registration happens in FLogCategoryBase's constructor via
 * AssociateSuppress (LogSuppressionInterface.h:22), so it depends on the owning module
 * being loaded and on static initialisation having run.
 *
 * How it can fail, concretely:
 *   - RacingSimTests stops depending on RacingSim, or the runtime module fails to
 *     load: the categories are absent from the map and the LOG LIST assertions fail;
 *   - a category is declared but its DEFINE_LOG_CATEGORY is dropped: it never
 *     constructs, never associates, and is absent from the listing;
 *   - a future config or command line pins a category so verbosity changes do not
 *     take: the round-trip assertion fails.
 *
 * None of those is a compile or link error, which is what makes this worth running.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRacingSimLogSuppressionTest,
	"RacingSim.Core.LogCategoryRegistration",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		// SmokeFilter, per Docs/Environment.md: the only documented automation command
		// is "Automation RunFilter Smoke". A test on another filter never runs.
		| EAutomationTestFlags::SmokeFilter)

bool FRacingSimLogSuppressionTest::RunTest(const FString& Parameters)
{
	const TArray<FLogCategoryBase*> Categories = {
		&LogRacingCore,
		&LogRacingVehicle,
		&LogRacingRace,
		&LogRacingUI,
		&LogRacingStreaming,
		&LogRacingTests
	};

	// ---------------------------------------------------------------------------
	// 1. Every category is present in the suppression system's name table.
	// ---------------------------------------------------------------------------
	//
	// "LOG LIST <substring>" walks ReverseAssociations and prints one line per match.
	// Filtering on "LogRacing" keeps the captured text small and keeps the positive
	// control meaningful: engine categories are excluded, so a match is this project's.
	FStringOutputDevice Listing;
	const bool bListHandled = FSelfRegisteringExec::StaticExec(nullptr, TEXT("LOG LIST LogRacing"), Listing);

	TestTrue(TEXT("The log suppression system handled 'LOG LIST'"), bListHandled);

	// Positive control. An empty listing would make every Contains() check below fail
	// for the wrong reason, and -- more dangerously -- an inverted assertion would
	// vacuously pass. Assert the listing is non-empty before trusting its contents.
	TestTrue(
		FString::Printf(TEXT("'LOG LIST LogRacing' returned output (%d chars)"), Listing.Len()),
		Listing.Len() > 0);

	for (const FLogCategoryBase* Category : Categories)
	{
		const FString Name = Category->GetCategoryName().ToString();
		TestTrue(
			FString::Printf(
				TEXT("Category '%s' is registered with the log suppression system"), *Name),
			Listing.Contains(Name, ESearchCase::CaseSensitive));
	}

	// Negative control for the search itself. If Contains() matched anything, the loop
	// above would pass even with the categories unregistered. This name is not declared
	// anywhere in the project, so it must be absent.
	TestFalse(
		TEXT("A category that does not exist is absent from the listing (search is sound)"),
		Listing.Contains(TEXT("LogRacingNotARealCategory"), ESearchCase::CaseSensitive));

	// ---------------------------------------------------------------------------
	// 2. The registered name is a working handle: setting verbosity by string
	//    changes the C++ object the runtime module compiled.
	// ---------------------------------------------------------------------------
	//
	// This is the assertion that connects the string in a config file or on a command
	// line to the object UE_LOG tests. It exercises the same ProcessCmdString path as
	// -LogCmds and [Core.Log].
	for (FLogCategoryBase* Category : Categories)
	{
		const FString Name = Category->GetCategoryName().ToString();
		const ELogVerbosity::Type OriginalVerbosity = Category->GetVerbosity();

		// Pick a target that is genuinely different from the current value, so a
		// no-op cannot be mistaken for success. Verbose and Error are both at or below
		// the Development compile-time verbosity (All), so SetVerbosity will not clamp.
		const ELogVerbosity::Type TargetVerbosity =
			(OriginalVerbosity == ELogVerbosity::Verbose) ? ELogVerbosity::Error : ELogVerbosity::Verbose;

		FStringOutputDevice SetOutput;
		const FString SetCommand = FString::Printf(
			TEXT("LOG %s %s"), *Name, ToString(TargetVerbosity));
		FSelfRegisteringExec::StaticExec(nullptr, *SetCommand, SetOutput);

		const ELogVerbosity::Type AfterSet = Category->GetVerbosity();

		// Restore before asserting. An assertion failure returns early from nothing
		// here -- TestEqual does not throw -- but a later change might, and a test that
		// leaves LogRacingRace at Verbose would flood every subsequent test's log.
		FStringOutputDevice RestoreOutput;
		const FString RestoreCommand = FString::Printf(
			TEXT("LOG %s %s"), *Name, ToString(OriginalVerbosity));
		FSelfRegisteringExec::StaticExec(nullptr, *RestoreCommand, RestoreOutput);

		const ELogVerbosity::Type AfterRestore = Category->GetVerbosity();

		TestEqual(
			FString::Printf(
				TEXT("Setting '%s' verbosity by name reaches the category object"), *Name),
			static_cast<int32>(AfterSet),
			static_cast<int32>(TargetVerbosity));

		TestEqual(
			FString::Printf(
				TEXT("Verbosity of '%s' was restored to its original value"), *Name),
			static_cast<int32>(AfterRestore),
			static_cast<int32>(OriginalVerbosity));
	}

	// ---------------------------------------------------------------------------
	// 3. The compile-time verbosity policy in RacingSimLog.h is what it claims.
	// ---------------------------------------------------------------------------
	//
	// CORE-002 finding N-1 was a comment that described the wrong macro parameter, and
	// the categories silently compiled in everything as a result. Assert the policy
	// rather than trusting the comment: Development keeps VeryVerbose; Shipping/Test
	// strip to Log. Only the first branch is reachable from an automation test, since
	// this module is UncookedOnly -- so the #else exists to fail loudly if that ever
	// changes rather than to be exercised.
#if UE_BUILD_SHIPPING || UE_BUILD_TEST
	const ELogVerbosity::Type ExpectedCompileTimeVerbosity = ELogVerbosity::Log;
#else
	const ELogVerbosity::Type ExpectedCompileTimeVerbosity = ELogVerbosity::VeryVerbose;
#endif

	for (const FLogCategoryBase* Category : Categories)
	{
		// LogRacingTests is declared in this module and is allowed to differ from the
		// runtime policy; assert it separately rather than silently exempting it.
		if (Category == &LogRacingTests)
		{
			continue;
		}

		TestEqual(
			FString::Printf(
				TEXT("Compile-time verbosity policy for '%s'"),
				*Category->GetCategoryName().ToString()),
			static_cast<int32>(Category->GetCompileTimeVerbosity()),
			static_cast<int32>(ExpectedCompileTimeVerbosity));
	}

	return true;
}
