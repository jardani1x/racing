// Copyright RacingSim. All Rights Reserved.

#include "Core/RacingSimBuildId.h"
#include "Core/RacingSimUrl.h"

#include "Misc/AutomationTest.h"

/**
 * CORE-003 finding C3-4, closed at RACE-003: percent-encoding for URL query values.
 *
 * WHY THIS IS A TEST AND NOT A REVIEW COMMENT. The hazard is invisible: an unencoded '+'
 * in a query string is not a parse error, it is a SILENT substitution to a space by every
 * `application/x-www-form-urlencoded` decoder there is. A build ID that arrives mangled
 * still looks like a build ID, and two builds whose IDs differ only either side of the
 * '+' collide into one. Nothing downstream can detect that after the fact, so it has to
 * be prevented at the encoder and asserted here.
 *
 * The obligation was written into Core/RacingSimBuildId.cpp's SanitiseComponent as a rule
 * for consumers to follow, with no implementation and no test, when CORE-003 decided to
 * allow '+' in the allow-list. This file is the other half of that decision.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRacingSimUrlEncodingTest,
	"RacingSim.Core.UrlEncoding",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FRacingSimUrlEncodingTest::RunTest(const FString& Parameters)
{
	using namespace RacingSim::Url;

	// -- The unreserved set survives verbatim --------------------------------
	//
	// RFC 3986's unreserved set is A-Z a-z 0-9 - . _ ~ and nothing else. An encoder that
	// escaped these would still be CORRECT, but it would make every build ID unreadable in
	// a log for no benefit, so the round-trip is pinned.
	{
		const FString Unreserved = TEXT("abcxyzABCXYZ0189-._~");
		TestEqual(TEXT("The unreserved set passes through unchanged"),
			PercentEncodeQueryValue(Unreserved), Unreserved);
	}

	// -- '+' IS THE ONE THAT MATTERS -----------------------------------------
	{
		TestEqual(TEXT("A bare '+' becomes %2B, not a space and not itself"),
			PercentEncodeQueryValue(TEXT("+")), FString(TEXT("%2B")));

		// The exact shape the Derived scheme produces: the engine changelist separator.
		const FString DerivedShaped = TEXT("dev-0.1.0-5.8.1+56057345-Development-Editor");
		const FString Encoded = PercentEncodeQueryValue(DerivedShaped);

		TestEqual(TEXT("A Derived-shaped build ID encodes its changelist separator"),
			Encoded, FString(TEXT("dev-0.1.0-5.8.1%2B56057345-Development-Editor")));
		TestFalse(TEXT("...leaving no bare '+' anywhere, which would decode to a space"),
			Encoded.Contains(TEXT("+")));
	}

	// -- The other characters that would break a URL outright -----------------
	//
	// '#' truncates the URL at a fragment boundary and '&'/'=' invent new key/value pairs.
	// FRacingContentVersion::ToString() emits BOTH '@' and '#'
	// ("Track.Prototype.Meridian@2#1a2b3c4d"), so this is not hypothetical -- it is the
	// exact string FRacingRaceResult::MakeSubmissionQueryString writes for track, car and
	// ruleset.
	{
		TestEqual(TEXT("'#' is encoded: unencoded it would truncate the URL"),
			PercentEncodeQueryValue(TEXT("#")), FString(TEXT("%23")));
		TestEqual(TEXT("'@' is encoded"), PercentEncodeQueryValue(TEXT("@")), FString(TEXT("%40")));
		TestEqual(TEXT("'&' is encoded: unencoded it would start a new query pair"),
			PercentEncodeQueryValue(TEXT("&")), FString(TEXT("%26")));
		TestEqual(TEXT("'=' is encoded"), PercentEncodeQueryValue(TEXT("=")), FString(TEXT("%3D")));
		TestEqual(TEXT("A space is encoded as %20, never as '+'"),
			PercentEncodeQueryValue(TEXT(" ")), FString(TEXT("%20")));
		TestEqual(TEXT("'/' and '?' are encoded"),
			PercentEncodeQueryValue(TEXT("/?")), FString(TEXT("%2F%3F")));
		TestEqual(TEXT("'%' itself is encoded, or decoding would be ambiguous"),
			PercentEncodeQueryValue(TEXT("%")), FString(TEXT("%25")));

		// A real FRacingContentVersion string, end to end.
		FRacingContentVersion Version;
		Version.AssetId = FName(TEXT("Track.Prototype.Meridian"));
		Version.SchemaVersion = 2;
		Version.ContentHash = 0x1A2B3C4D;
		const FString EncodedVersion = PercentEncodeQueryValue(Version.ToString());
		TestEqual(TEXT("A content version encodes both its separators"),
			EncodedVersion, FString(TEXT("Track.Prototype.Meridian%402%231a2b3c4d")));
	}

	// -- Non-ASCII encodes as UTF-8 BYTES, not as UTF-16 code units -----------
	//
	// A percent triplet is defined over octets. Encoding a TCHAR directly would emit a
	// four-digit "%XXXX", which no decoder accepts. Build ids are ASCII by
	// SanitiseComponent's allow-list, but a track id is authored FName text and carries no
	// such guarantee, so the byte path has to be right rather than merely unreached.
	{
		// U+00E9 LATIN SMALL LETTER E WITH ACUTE is 0xC3 0xA9 in UTF-8.
		const FString Accented = FString(TEXT("caf")) + FString::Chr(0x00E9);
		TestEqual(TEXT("A two-byte character encodes as two triplets"),
			PercentEncodeQueryValue(Accented), FString(TEXT("caf%C3%A9")));
	}

	// -- Degenerate input -----------------------------------------------------
	{
		TestEqual(TEXT("Empty in, empty out"), PercentEncodeQueryValue(FString()), FString());
	}

	// -- The accessor the rest of the project actually calls -------------------
	{
		FRacingSimBuildId Handmade;
		Handmade.Value = TEXT("ci-2026.08.21+4417");
		TestEqual(TEXT("FRacingSimBuildId::ToUrlQueryValue encodes its own Value"),
			Handmade.ToUrlQueryValue(), FString(TEXT("ci-2026.08.21%2B4417")));

		// And the LIVE build ID, whichever scheme this machine is configured for. This is
		// the assertion that keeps working when the scheme, the channel or the engine
		// changelist changes -- it does not care what the ID is, only that the encoded form
		// cannot be silently mangled by a query-string decoder.
		const FRacingSimBuildId Live = FRacingSimBuildId::Current();
		const FString LiveEncoded = Live.ToUrlQueryValue();
		TestFalse(TEXT("The running build's encoded ID contains no bare '+'"), LiveEncoded.Contains(TEXT("+")));
		TestFalse(TEXT("...no bare '&'"), LiveEncoded.Contains(TEXT("&")));
		TestFalse(TEXT("...no bare '#'"), LiveEncoded.Contains(TEXT("#")));
		TestFalse(TEXT("...and no space"), LiveEncoded.Contains(TEXT(" ")));
		TestTrue(TEXT("...and it is non-empty, because a build ID always is"), !LiveEncoded.IsEmpty());
	}

	return true;
}
