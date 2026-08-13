// Copyright RacingSim. All Rights Reserved.

#include "Core/RacingSimBuildId.h"

#include "Core/RacingSimLog.h"
#include "Core/RacingSimSettings.h"
// LexToString(EBuildConfiguration) and LexToString(EBuildTargetType) both return
// const TCHAR*, so they are safe as %s arguments to FString::Printf. Verified in
// Engine/Source/Runtime/Core/Public/GenericPlatform/GenericPlatformMisc.h:103,155.
#include "GenericPlatform/GenericPlatformMisc.h"
#include "Misc/App.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/EngineVersion.h"

namespace RacingSim::BuildIdPrivate
{
	/** Component separator inside a derived build ID. */
	static const TCHAR* ComponentSeparator = TEXT("-");

	/**
	 * Strip anything that would make a composed ID ambiguous to parse or unsafe
	 * in a filename. Applied to every free-form component (channel, project
	 * version, explicit ID) so a stray space or slash in an ini cannot produce
	 * an ID that later code has to guess about.
	 */
	static FString SanitiseComponent(const FString& In)
	{
		FString Out;
		Out.Reserve(In.Len());
		for (const TCHAR Ch : In)
		{
			// Allow only characters that survive a filename, a URL and a CSV cell.
			// '-' included: it is exactly as safe as '.'/'_' in all three contexts,
			// and CI-stamped explicit IDs (e.g. "ci-2026.08.12-4417") must round-trip
			// verbatim -- see RacingSim.Core.BuildId's "used verbatim once sanitised" case.
			const bool bAllowed = FChar::IsAlnum(Ch) || Ch == TEXT('.') || Ch == TEXT('_') || Ch == TEXT('-');
			if (bAllowed)
			{
				Out.AppendChar(Ch);
			}
		}
		return Out;
	}

	/**
	 * Project version from Config/DefaultGame.ini, the same key the packaging
	 * settings write.
	 *
	 * Read through GConfig rather than by depending on the EngineSettings module
	 * for UGeneralProjectSettings: one config read is cheaper than a module
	 * dependency that Core/ would otherwise not need, and this is called once
	 * per race at most. Absent or blank is a normal state on a fresh project and
	 * yields "0.0.0" rather than an error.
	 */
	static FString GetProjectVersion()
	{
		FString ProjectVersion;
		if (GConfig != nullptr)
		{
			GConfig->GetString(
				TEXT("/Script/EngineSettings.GeneralProjectSettings"),
				TEXT("ProjectVersion"),
				ProjectVersion,
				GGameIni);
		}

		ProjectVersion = SanitiseComponent(ProjectVersion.TrimStartAndEnd());
		return ProjectVersion.IsEmpty() ? TEXT("0.0.0") : ProjectVersion;
	}
}

FString FRacingContentVersion::ToString() const
{
	// "<AssetId>@<SchemaVersion>#<ContentHash as 8 hex digits>"
	return FString::Printf(
		TEXT("%s@%d#%08x"),
		AssetId.IsNone() ? TEXT("none") : *AssetId.ToString(),
		SchemaVersion,
		ContentHash);
}

FRacingSimBuildId FRacingSimBuildId::Current()
{
	using namespace RacingSim::BuildIdPrivate;

	const URacingSimSettings& Settings = URacingSimSettings::Get();

	FRacingSimBuildId Result;
	Result.Scheme = Settings.BuildIdScheme;

	if (Settings.BuildIdScheme == ERacingBuildIdScheme::Explicit)
	{
		const FString Trimmed = Settings.ExplicitBuildId.TrimStartAndEnd();
		const FString Stamped = SanitiseComponent(Trimmed);
		if (!Stamped.IsEmpty())
		{
			// Sanitisation can mutate a stamped ID (semver build metadata like
			// "+4417" or a branch-qualified stamp like "feature/x" both contain
			// characters this allow-list rejects). A silently mutated ID breaks
			// the authoritative promise: it may no longer trace back to the CI
			// run that produced it, and two distinct stamps can collapse to the
			// same sanitised string. Warn loudly with both values so CI is fixed
			// rather than trusting a corrupted-but-plausible ID forever.
			if (Stamped != Trimmed)
			{
				UE_LOG(
					LogRacingCore,
					Warning,
					TEXT("ExplicitBuildId was sanitised: stamped \"%s\" but recorded \"%s\". ")
					TEXT("Fix the CI-stamped value to use only [A-Za-z0-9._-] so it round-trips verbatim."),
					*Trimmed,
					*Stamped);
			}
			Result.Value = Stamped;
			Result.bIsAuthoritative = true;
			return Result;
		}

		// Explicit was requested and nothing was stamped. Fall through to the
		// derived form rather than returning an empty ID: a result with no build
		// ID at all is unusable, and a derived ID marked non-authoritative is
		// both usable and honest. Warn once per call site -- this is a
		// misconfigured CI job, and silence would let it ship.
		UE_LOG(
			LogRacingCore,
			Warning,
			TEXT("BuildIdScheme is Explicit but ExplicitBuildId is empty; falling back to a derived, "
				 "non-authoritative build ID. Results from this build must not be published."));
		Result.Scheme = ERacingBuildIdScheme::Derived;
	}

	// Derived form:
	//   <channel>-<projectversion>-<engineversion>+<changelist>-<config>-<targettype>
	// e.g. dev-0.1.0-5.8.1+56057345-Development-Editor
	const FEngineVersion& EngineVersion = FEngineVersion::Current();

	FString Channel = SanitiseComponent(Settings.BuildChannel.TrimStartAndEnd());
	if (Channel.IsEmpty())
	{
		Channel = TEXT("dev");
	}

	Result.Value = FString::Printf(
		TEXT("%s%s%s%s%s+%u%s%s%s%s"),
		*Channel,
		ComponentSeparator,
		*GetProjectVersion(),
		ComponentSeparator,
		*EngineVersion.ToString(EVersionComponent::Patch),
		EngineVersion.GetChangelist(),
		ComponentSeparator,
		LexToString(FApp::GetBuildConfiguration()),
		ComponentSeparator,
		LexToString(FApp::GetBuildTargetType()));

	// A derived ID is never authoritative, even in a packaged Shipping build:
	// two different local source trees on the same engine patch produce the same
	// string. That is exactly the case a leaderboard must not silently merge.
	Result.bIsAuthoritative = false;
	return Result;
}

FRacingSimVersionStamp FRacingSimVersionStamp::MakeCurrent()
{
	const URacingSimSettings& Settings = URacingSimSettings::Get();
	const FEngineVersion& EngineVersion = FEngineVersion::Current();

	FRacingSimVersionStamp Stamp;
	Stamp.GameBuildId = FRacingSimBuildId::Current();
	Stamp.EngineVersion = EngineVersion.ToString(EVersionComponent::Patch);
	Stamp.EngineChangelist = static_cast<int32>(EngineVersion.GetChangelist());
	Stamp.PhysicsPolicyVersion = Settings.PhysicsPolicyVersion;
	Stamp.AssistPreset = Settings.DefaultAssistPreset;

	// Left unpopulated ON PURPOSE. Filling these with plausible defaults here
	// would produce a stamp that passes IsPublishable() while describing nothing:
	//   TrackVersion    -> TRACK-001
	//   CarSpecVersion  -> VEH-003
	//   RulesetVersion  -> RACE-001
	//   InputDeviceType -> VEH-001 (input component), at run start
	//   Validity        -> RACE-002/RACE-003, at run end
	Stamp.Validity = ERacingRunValidity::Pending;
	return Stamp;
}

bool FRacingSimVersionStamp::IsPublishable(FString* OutReason) const
{
	auto Fail = [OutReason](const TCHAR* Reason) -> bool
	{
		if (OutReason != nullptr)
		{
			*OutReason = Reason;
		}
		return false;
	};

	if (GameBuildId.Value.IsEmpty())
	{
		return Fail(TEXT("GameBuildId is empty"));
	}
	if (!GameBuildId.bIsAuthoritative)
	{
		// The one rule this whole scheme exists to enforce: a Derived build ID
		// cannot distinguish two different local source trees built from the
		// same engine patch, so a developer-machine result must never reach a
		// published leaderboard, however complete the rest of the stamp is.
		return Fail(TEXT("GameBuildId is not authoritative (Derived scheme); developer results cannot be published"));
	}
	if (EngineVersion.IsEmpty())
	{
		return Fail(TEXT("EngineVersion is empty"));
	}
	if (!TrackVersion.IsPopulated())
	{
		return Fail(TEXT("TrackVersion is not populated (owner: TRACK-001)"));
	}
	if (!CarSpecVersion.IsPopulated())
	{
		return Fail(TEXT("CarSpecVersion is not populated (owner: VEH-003)"));
	}
	if (!RulesetVersion.IsPopulated())
	{
		return Fail(TEXT("RulesetVersion is not populated (owner: RACE-001)"));
	}
	if (PhysicsPolicyVersion <= 0)
	{
		return Fail(TEXT("PhysicsPolicyVersion is 0 (no physics policy established; owner: VEH-002)"));
	}
	if (InputDeviceType == ERacingInputDeviceType::Unknown)
	{
		return Fail(TEXT("InputDeviceType is Unknown"));
	}
	if (Validity == ERacingRunValidity::Unknown || Validity == ERacingRunValidity::Pending)
	{
		return Fail(TEXT("Validity is not terminal"));
	}

	if (OutReason != nullptr)
	{
		OutReason->Reset();
	}
	return true;
}

bool FRacingSimVersionStamp::IsComparableTo(const FRacingSimVersionStamp& Other) const
{
	// Unpopulated data is never comparable, not even to identically unpopulated
	// data: two stamps that both know nothing about their track are not two runs
	// on the same track. Without this, a pair of default-constructed stamps would
	// compare equal and produce a leaderboard of unrelated laps.
	if (!TrackVersion.IsPopulated() || !Other.TrackVersion.IsPopulated())
	{
		return false;
	}
	if (!CarSpecVersion.IsPopulated() || !Other.CarSpecVersion.IsPopulated())
	{
		return false;
	}
	if (!RulesetVersion.IsPopulated() || !Other.RulesetVersion.IsPopulated())
	{
		return false;
	}
	if (PhysicsPolicyVersion <= 0 || Other.PhysicsPolicyVersion <= 0)
	{
		return false;
	}

	return TrackVersion == Other.TrackVersion
		&& CarSpecVersion == Other.CarSpecVersion
		&& RulesetVersion == Other.RulesetVersion
		&& PhysicsPolicyVersion == Other.PhysicsPolicyVersion
		&& AssistPreset == Other.AssistPreset;
}

FString FRacingSimVersionStamp::ToString() const
{
	return FString::Printf(
		TEXT("build=%s(%s%s) engine=%s+%d track=%s car=%s ruleset=%s physics=%d assists=%d input=%d validity=%d penalties=%d/%.3fs"),
		*GameBuildId.Value,
		GameBuildId.Scheme == ERacingBuildIdScheme::Explicit ? TEXT("explicit") : TEXT("derived"),
		GameBuildId.bIsAuthoritative ? TEXT("") : TEXT(",non-authoritative"),
		*EngineVersion,
		EngineChangelist,
		*TrackVersion.ToString(),
		*CarSpecVersion.ToString(),
		*RulesetVersion.ToString(),
		PhysicsPolicyVersion,
		static_cast<int32>(AssistPreset),
		static_cast<int32>(InputDeviceType),
		static_cast<int32>(Validity),
		Penalties.PenaltyCount,
		Penalties.TotalPenaltySeconds);
}
