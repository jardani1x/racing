// Copyright RacingSim. All Rights Reserved.

#include "Core/RacingSimBuildId.h"
#include "Core/RacingSimSettings.h"
#include "Core/RacingSimTypes.h"
#include "Misc/AutomationTest.h"
#include "Misc/EngineVersion.h"

namespace RacingSimVersionSpecPrivate
{
	/**
	 * Restores every settings field this test touches, whatever happens.
	 *
	 * The tests below mutate the settings CDO, which is process-global and shared
	 * with everything else in the editor. Restoring on scope exit (including an
	 * early return or an ensure) is the difference between a test and a landmine
	 * for whatever runs next. Nothing here calls SaveConfig(), so no ini is written.
	 */
	struct FScopedSettingsOverride
	{
		FScopedSettingsOverride()
			: Settings(GetMutableDefault<URacingSimSettings>())
			, SavedScheme(Settings->BuildIdScheme)
			, SavedChannel(Settings->BuildChannel)
			, SavedExplicitId(Settings->ExplicitBuildId)
			, SavedPhysicsPolicyVersion(Settings->PhysicsPolicyVersion)
			, SavedAssistPreset(Settings->DefaultAssistPreset)
		{
		}

		~FScopedSettingsOverride()
		{
			Settings->BuildIdScheme = SavedScheme;
			Settings->BuildChannel = SavedChannel;
			Settings->ExplicitBuildId = SavedExplicitId;
			Settings->PhysicsPolicyVersion = SavedPhysicsPolicyVersion;
			Settings->DefaultAssistPreset = SavedAssistPreset;
		}

		URacingSimSettings* Settings;

	private:
		ERacingBuildIdScheme SavedScheme;
		FString SavedChannel;
		FString SavedExplicitId;
		int32 SavedPhysicsPolicyVersion;
		ERacingAssistPreset SavedAssistPreset;
	};

	/** A fully populated content version, for comparability cases. */
	static FRacingContentVersion MakeVersion(const TCHAR* Id, const int32 Schema, const uint32 Hash)
	{
		FRacingContentVersion Version;
		Version.AssetId = FName(Id);
		Version.SchemaVersion = Schema;
		Version.ContentHash = Hash;
		return Version;
	}

	/** A stamp that would be publishable, as the baseline for the negative cases. */
	static FRacingSimVersionStamp MakePublishableStamp()
	{
		FRacingSimVersionStamp Stamp;
		Stamp.GameBuildId.Value = TEXT("ci-1.2.3");
		Stamp.GameBuildId.Scheme = ERacingBuildIdScheme::Explicit;
		Stamp.GameBuildId.bIsAuthoritative = true;
		Stamp.EngineVersion = TEXT("5.8.1");
		Stamp.EngineChangelist = 56057345;
		Stamp.TrackVersion = MakeVersion(TEXT("Track.Prototype.A"), 1, 0xAAAAAAAA);
		Stamp.CarSpecVersion = MakeVersion(TEXT("Car.Prototype.A"), 1, 0xBBBBBBBB);
		Stamp.RulesetVersion = MakeVersion(TEXT("Ruleset.TimeAttack"), 1, 0xCCCCCCCC);
		Stamp.PhysicsPolicyVersion = 1;
		Stamp.AssistPreset = ERacingAssistPreset::Raw;
		Stamp.InputDeviceType = ERacingInputDeviceType::Gamepad;
		Stamp.Validity = ERacingRunValidity::Valid;
		return Stamp;
	}
}

/**
 * CORE-002: build ID resolution.
 *
 * The property that matters is not the exact string -- it is that a build ID is
 * never silently absent and never falsely authoritative. A leaderboard that
 * accepts a derived ID merges results from source trees that have nothing in
 * common, and it does so invisibly.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRacingSimBuildIdTest,
	"RacingSim.Core.BuildId",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FRacingSimBuildIdTest::RunTest(const FString& Parameters)
{
	using namespace RacingSimVersionSpecPrivate;

	FScopedSettingsOverride Override;

	// -- Derived scheme -----------------------------------------------------
	{
		Override.Settings->BuildIdScheme = ERacingBuildIdScheme::Derived;
		Override.Settings->BuildChannel = TEXT("dev");
		Override.Settings->ExplicitBuildId.Reset();

		const FRacingSimBuildId BuildId = FRacingSimBuildId::Current();

		TestFalse(TEXT("Derived build ID is not empty"), BuildId.Value.IsEmpty());
		TestEqual(TEXT("Derived scheme is reported"), BuildId.Scheme, ERacingBuildIdScheme::Derived);
		TestFalse(TEXT("A derived build ID is never authoritative"), BuildId.bIsAuthoritative);
		TestTrue(TEXT("Derived ID carries the channel"), BuildId.Value.StartsWith(TEXT("dev-")));

		// The engine patch must be in the ID, otherwise the ID cannot distinguish
		// builds made on different engine versions -- the thing it exists for.
		const FString EngineVersionString = FEngineVersion::Current().ToString(EVersionComponent::Patch);
		TestTrue(
			*FString::Printf(TEXT("Derived ID contains the engine version %s (got %s)"), *EngineVersionString, *BuildId.Value),
			BuildId.Value.Contains(EngineVersionString));

		// Whitespace in config must not reach the ID: it would break any tool
		// that splits a results file on spaces.
		TestFalse(TEXT("Derived ID contains no whitespace"), BuildId.Value.Contains(TEXT(" ")));

		// Determinism. Two calls in the same process describe the same build.
		TestEqual(TEXT("Build ID resolution is deterministic"), FRacingSimBuildId::Current().Value, BuildId.Value);
	}

	// -- Channel sanitisation -----------------------------------------------
	{
		Override.Settings->BuildChannel = TEXT("  release candidate/2  ");
		const FRacingSimBuildId BuildId = FRacingSimBuildId::Current();
		TestTrue(
			*FString::Printf(TEXT("Unsafe channel characters are stripped (got %s)"), *BuildId.Value),
			BuildId.Value.StartsWith(TEXT("releasecandidate2-")));
	}

	// An empty channel must not produce a leading separator.
	{
		Override.Settings->BuildChannel.Reset();
		const FRacingSimBuildId BuildId = FRacingSimBuildId::Current();
		TestTrue(TEXT("Empty channel falls back to dev"), BuildId.Value.StartsWith(TEXT("dev-")));
	}

	// A hyphenated channel is not an error: a derived ID is opaque and never
	// parsed back into components, so an embedded '-' is not ambiguous.
	{
		Override.Settings->BuildChannel = TEXT("release-candidate");
		const FRacingSimBuildId BuildId = FRacingSimBuildId::Current();
		TestTrue(
			*FString::Printf(TEXT("Hyphenated channel is preserved verbatim (got %s)"), *BuildId.Value),
			BuildId.Value.StartsWith(TEXT("release-candidate-")));
	}

	// -- Explicit scheme ----------------------------------------------------
	{
		Override.Settings->BuildIdScheme = ERacingBuildIdScheme::Explicit;
		Override.Settings->ExplicitBuildId = TEXT("ci-2026.08.12-4417");

		const FRacingSimBuildId BuildId = FRacingSimBuildId::Current();
		TestEqual(TEXT("Explicit ID is used verbatim once sanitised"), BuildId.Value, FString(TEXT("ci-2026.08.12-4417")));
		TestEqual(TEXT("Explicit scheme is reported"), BuildId.Scheme, ERacingBuildIdScheme::Explicit);
		TestTrue(TEXT("A stamped explicit ID is authoritative"), BuildId.bIsAuthoritative);
	}

	// -- Explicit ID mutated by sanitisation ---------------------------------
	// Semver build metadata ("+4417") and branch-qualified stamps ("feature/x")
	// both contain characters the allow-list rejects. Sanitisation must not
	// silently corrupt an authoritative ID -- it must warn with both values so
	// CI is fixed, per H-2.
	{
		// Pattern includes the raw stamped value so it is distinct from the
		// "feature/x" case below -- AddExpectedMessage keys a TSet by pattern
		// string alone, so two identical patterns would make the second
		// registration silently replace the first (each proving nothing).
		// Including the value also proves the *right* input was reported, not
		// just that some warning fired.
		AddExpectedMessagePlain(
			TEXT("ExplicitBuildId was sanitised: stamped \"1.4.0+4417\""),
			ELogVerbosity::Warning,
			EAutomationExpectedMessageFlags::Contains,
			1);

		Override.Settings->BuildIdScheme = ERacingBuildIdScheme::Explicit;
		Override.Settings->ExplicitBuildId = TEXT("1.4.0+4417");

		const FRacingSimBuildId BuildId = FRacingSimBuildId::Current();
		TestEqual(TEXT("Sanitised explicit ID drops the disallowed '+'"), BuildId.Value, FString(TEXT("1.4.04417")));
		TestTrue(TEXT("A sanitised-but-nonempty explicit ID is still authoritative"), BuildId.bIsAuthoritative);
	}
	{
		AddExpectedMessagePlain(
			TEXT("ExplicitBuildId was sanitised: stamped \"feature/x\""),
			ELogVerbosity::Warning,
			EAutomationExpectedMessageFlags::Contains,
			1);

		Override.Settings->BuildIdScheme = ERacingBuildIdScheme::Explicit;
		Override.Settings->ExplicitBuildId = TEXT("feature/x");

		const FRacingSimBuildId BuildId = FRacingSimBuildId::Current();
		TestEqual(TEXT("Sanitised explicit ID drops the disallowed '/'"), BuildId.Value, FString(TEXT("featurex")));
	}

	// -- Explicit requested but not stamped ---------------------------------
	// The misconfigured-CI case. It must warn, fall back, and stay
	// non-authoritative -- never return an empty ID, and never claim authority.
	{
		AddExpectedMessagePlain(
			TEXT("BuildIdScheme is Explicit but ExplicitBuildId is empty"),
			ELogVerbosity::Warning,
			EAutomationExpectedMessageFlags::Contains,
			1);

		Override.Settings->BuildIdScheme = ERacingBuildIdScheme::Explicit;
		Override.Settings->ExplicitBuildId = TEXT("   ");
		Override.Settings->BuildChannel = TEXT("ci");

		const FRacingSimBuildId BuildId = FRacingSimBuildId::Current();
		TestFalse(TEXT("Fallback ID is not empty"), BuildId.Value.IsEmpty());
		TestEqual(TEXT("Fallback reports the Derived scheme it actually used"), BuildId.Scheme, ERacingBuildIdScheme::Derived);
		TestFalse(TEXT("Fallback ID is not authoritative"), BuildId.bIsAuthoritative);
	}

	return true;
}

/**
 * CORE-002: version stamp contract.
 *
 * Two rules under test, both of which exist to stop a wrong leaderboard rather
 * than to make one work:
 *
 *   IsPublishable  -- a stamp with an unpopulated field is refused, and says which.
 *   IsComparableTo -- unpopulated data is never comparable, not even to itself.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRacingSimVersionStampTest,
	"RacingSim.Core.VersionStamp",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FRacingSimVersionStampTest::RunTest(const FString& Parameters)
{
	using namespace RacingSimVersionSpecPrivate;

	// -- MakeCurrent fills only what this build knows ------------------------
	{
		FScopedSettingsOverride Override;
		Override.Settings->BuildIdScheme = ERacingBuildIdScheme::Derived;
		Override.Settings->PhysicsPolicyVersion = 0;
		Override.Settings->DefaultAssistPreset = ERacingAssistPreset::Raw;

		const FRacingSimVersionStamp Stamp = FRacingSimVersionStamp::MakeCurrent();

		TestFalse(TEXT("MakeCurrent fills the build ID"), Stamp.GameBuildId.Value.IsEmpty());
		TestFalse(TEXT("MakeCurrent fills the engine version"), Stamp.EngineVersion.IsEmpty());
		TestTrue(TEXT("MakeCurrent fills the engine changelist"), Stamp.EngineChangelist > 0);
		TestEqual(TEXT("MakeCurrent reads the configured assist preset"), Stamp.AssistPreset, ERacingAssistPreset::Raw);
		TestEqual(TEXT("MakeCurrent reads the configured physics policy version"), Stamp.PhysicsPolicyVersion, 0);

		// Deliberately unpopulated: these belong to TRACK-001, VEH-003, RACE-001,
		// VEH-001 and RACE-002. Guessing them is what would produce a stamp that
		// passes validation while describing nothing.
		TestFalse(TEXT("Track version is left to TRACK-001"), Stamp.TrackVersion.IsPopulated());
		TestFalse(TEXT("Car spec version is left to VEH-003"), Stamp.CarSpecVersion.IsPopulated());
		TestFalse(TEXT("Ruleset version is left to RACE-001"), Stamp.RulesetVersion.IsPopulated());
		TestEqual(TEXT("Input device is left to VEH-001"), Stamp.InputDeviceType, ERacingInputDeviceType::Unknown);
		TestEqual(TEXT("Validity starts Pending"), Stamp.Validity, ERacingRunValidity::Pending);
		TestTrue(TEXT("A fresh stamp has no penalties"), Stamp.Penalties.IsClean());

		// The single most important assertion in this file: nothing produced by
		// this build, today, may be published as a competitive result.
		FString Reason;
		TestFalse(TEXT("A freshly made stamp is NOT publishable"), Stamp.IsPublishable(&Reason));
		TestFalse(TEXT("Refusal explains itself"), Reason.IsEmpty());
	}

	// -- IsPublishable: one missing field at a time --------------------------
	{
		const FRacingSimVersionStamp Good = MakePublishableStamp();
		FString Reason;
		TestTrue(TEXT("A complete stamp is publishable"), Good.IsPublishable(&Reason));
		TestTrue(TEXT("A passing stamp clears the reason"), Reason.IsEmpty());

		auto ExpectRefused = [this](const FRacingSimVersionStamp& Stamp, const TCHAR* What)
		{
			FString LocalReason;
			TestFalse(What, Stamp.IsPublishable(&LocalReason));
			TestFalse(*FString::Printf(TEXT("%s -- with a reason"), What), LocalReason.IsEmpty());
		};

		{
			FRacingSimVersionStamp S = Good;
			S.GameBuildId.Value.Reset();
			ExpectRefused(S, TEXT("Refused without a build ID"));
		}
		{
			FRacingSimVersionStamp S = Good;
			S.EngineVersion.Reset();
			ExpectRefused(S, TEXT("Refused without an engine version"));
		}
		{
			// The invariant this ticket exists to establish: a complete stamp is
			// still refused if its build ID came from the Derived scheme. Every
			// other field here is fully populated, so this isolates the check.
			FRacingSimVersionStamp S = Good;
			S.GameBuildId.bIsAuthoritative = false;
			ExpectRefused(S, TEXT("Refused with a non-authoritative (Derived) build ID"));
		}
		{
			FRacingSimVersionStamp S = Good;
			S.TrackVersion = FRacingContentVersion();
			ExpectRefused(S, TEXT("Refused without a track version"));
		}
		{
			FRacingSimVersionStamp S = Good;
			S.CarSpecVersion = FRacingContentVersion();
			ExpectRefused(S, TEXT("Refused without a car spec version"));
		}
		{
			FRacingSimVersionStamp S = Good;
			S.RulesetVersion = FRacingContentVersion();
			ExpectRefused(S, TEXT("Refused without a ruleset version"));
		}
		{
			FRacingSimVersionStamp S = Good;
			S.PhysicsPolicyVersion = 0;
			ExpectRefused(S, TEXT("Refused without a physics policy version"));
		}
		{
			FRacingSimVersionStamp S = Good;
			S.InputDeviceType = ERacingInputDeviceType::Unknown;
			ExpectRefused(S, TEXT("Refused with an unknown input device"));
		}
		{
			FRacingSimVersionStamp S = Good;
			S.Validity = ERacingRunValidity::Pending;
			ExpectRefused(S, TEXT("Refused while validity is still Pending"));
		}
		{
			FRacingSimVersionStamp S = Good;
			S.Validity = ERacingRunValidity::Unknown;
			ExpectRefused(S, TEXT("Refused while validity is Unknown"));
		}

		// An invalid run is still publishable: it is a real, explained result and
		// the leaderboard is entitled to show it as invalidated. Only an
		// *undecided* validity is refused.
		{
			FRacingSimVersionStamp S = Good;
			S.Validity = ERacingRunValidity::InvalidShortcut;
			FString LocalReason;
			TestTrue(TEXT("A decided-invalid run is still publishable"), S.IsPublishable(&LocalReason));
		}

		// IsPublishable must tolerate a null out-parameter -- callers that only
		// want the bool are the common case.
		TestTrue(TEXT("IsPublishable works without an out reason"), Good.IsPublishable(nullptr));
	}

	// -- IsComparableTo ------------------------------------------------------
	{
		const FRacingSimVersionStamp A = MakePublishableStamp();

		TestTrue(TEXT("A stamp is comparable to an identical stamp"), A.IsComparableTo(A));

		// Symmetry: an asymmetric comparability rule produces leaderboards whose
		// contents depend on iteration order.
		{
			FRacingSimVersionStamp B = A;
			TestTrue(TEXT("Comparability is symmetric"), A.IsComparableTo(B) && B.IsComparableTo(A));
		}

		// Empty stamps must NOT be comparable, even though every compared field
		// is trivially equal. This is the case that would otherwise build a
		// leaderboard out of unrelated laps.
		{
			const FRacingSimVersionStamp Empty;
			TestFalse(TEXT("Two empty stamps are not comparable"), Empty.IsComparableTo(Empty));
		}

		{
			FRacingSimVersionStamp B = A;
			B.TrackVersion.ContentHash = 0x12345678;
			TestFalse(TEXT("A retuned track breaks comparability"), A.IsComparableTo(B));
		}
		{
			FRacingSimVersionStamp B = A;
			B.TrackVersion.SchemaVersion = 2;
			TestFalse(TEXT("A track schema bump breaks comparability"), A.IsComparableTo(B));
		}
		{
			FRacingSimVersionStamp B = A;
			B.CarSpecVersion.ContentHash = 0x99999999;
			TestFalse(TEXT("A retuned car breaks comparability"), A.IsComparableTo(B));
		}
		{
			FRacingSimVersionStamp B = A;
			B.RulesetVersion.ContentHash = 0x77777777;
			TestFalse(TEXT("A changed ruleset breaks comparability"), A.IsComparableTo(B));
		}
		{
			FRacingSimVersionStamp B = A;
			B.PhysicsPolicyVersion = 2;
			TestFalse(TEXT("A new physics policy breaks comparability"), A.IsComparableTo(B));
		}
		{
			FRacingSimVersionStamp B = A;
			B.AssistPreset = ERacingAssistPreset::FullyAssisted;
			TestFalse(TEXT("A different assist preset breaks comparability"), A.IsComparableTo(B));
		}

		// Documented and deliberate: these do NOT break comparability. If a later
		// ticket decides otherwise, it changes the rule here, on purpose.
		{
			FRacingSimVersionStamp B = A;
			B.GameBuildId.Value = TEXT("ci-9.9.9");
			TestTrue(TEXT("A different build ID does not split the board"), A.IsComparableTo(B));
		}
		{
			FRacingSimVersionStamp B = A;
			B.InputDeviceType = ERacingInputDeviceType::Wheel;
			TestTrue(TEXT("Input device does not split the board (RACE-003 may revisit)"), A.IsComparableTo(B));
		}
		{
			FRacingSimVersionStamp B = A;
			B.EngineChangelist = 99999999;
			TestTrue(TEXT("An engine changelist change does not split the board (known counter-case)"), A.IsComparableTo(B));
		}
	}

	// -- ToString ------------------------------------------------------------
	{
		const FRacingSimVersionStamp Stamp = MakePublishableStamp();
		const FString Text = Stamp.ToString();
		TestTrue(TEXT("ToString carries the build ID"), Text.Contains(TEXT("ci-1.2.3")));
		TestTrue(TEXT("ToString carries the engine version"), Text.Contains(TEXT("5.8.1")));
		TestTrue(TEXT("ToString carries the track asset id"), Text.Contains(TEXT("Track.Prototype.A")));

		const FRacingContentVersion Version = MakeVersion(TEXT("Track.Prototype.A"), 3, 0x1A2B3C4D);
		TestEqual(
			TEXT("Content version formats as id@schema#hash"),
			Version.ToString(),
			FString(TEXT("Track.Prototype.A@3#1a2b3c4d")));

		const FRacingContentVersion Unpopulated;
		TestEqual(
			TEXT("An unpopulated content version says so"),
			Unpopulated.ToString(),
			FString(TEXT("none@0#00000000")));
	}

	return true;
}
