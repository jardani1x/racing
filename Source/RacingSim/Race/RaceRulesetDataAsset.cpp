// Copyright RacingSim. All Rights Reserved.

#include "Race/RaceRulesetDataAsset.h"

uint32 URaceRulesetDataAsset::ComputeContentHash() const
{
	// GetTypeHash(double) hashes the bit pattern, so 3.0 and 3.0000001 differ, which
	// is the point: a retune must be visible. Two consequences worth stating rather
	// than discovering later -- +0.0 and -0.0 hash differently despite comparing
	// equal, and a NaN hashes stably but compares unequal to itself. Validate()
	// rejects non-finite values before a hash of one can reach a result.
	uint32 Hash = GetTypeHash(RulesetId);
	Hash = HashCombine(Hash, GetTypeHash(CountdownSeconds));

	// RACE-002. A ruleset that voids a lap on reset and one that does not are different
	// competitions on identical geometry, so the flag has to be visible on a result.
	Hash = HashCombine(Hash, GetTypeHash(bResetInvalidatesLap));

	// Any field added by RACE-003 must be combined in here as well.
	return Hash;
}

FRacingContentVersion URaceRulesetDataAsset::GetContentVersion() const
{
	FRacingContentVersion Version;
	Version.AssetId = RulesetId;
	Version.SchemaVersion = RulesetSchemaVersion;
	Version.ContentHash = ComputeContentHash();
	return Version;
}

bool URaceRulesetDataAsset::Validate(FString& OutReason) const
{
	if (RulesetId.IsNone())
	{
		OutReason = TEXT("RulesetId is None. An unnamed ruleset cannot identify itself on a result.");
		return false;
	}

	if (!FMath::IsFinite(CountdownSeconds))
	{
		OutReason = TEXT("CountdownSeconds is not finite.");
		return false;
	}

	if (CountdownSeconds < 0.0)
	{
		// A negative countdown would expire on the first poll, i.e. behave exactly
		// like 0.0 while reading as a deliberate value. Reject rather than clamp.
		OutReason = FString::Printf(TEXT("CountdownSeconds is negative (%f)."), CountdownSeconds);
		return false;
	}

	if (CountdownSeconds <= 0.0)
	{
		OutReason = TEXT("CountdownSeconds is zero. Valid for automation, but a published run must give the driver a countdown.");
		return false;
	}

	return true;
}
