// Copyright RacingSim. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/StringConv.h"

/**
 * CORE-003 finding C3-4, closed at RACE-003: percent-encoding for URL query values.
 *
 * ===========================================================================
 * Why this exists, and why it is in Core/
 * ===========================================================================
 *
 * FRacingSimBuildId's sanitiser (Core/RacingSimBuildId.cpp) deliberately allows '+'
 * in a build ID, because the Derived scheme already embeds one as the engine-changelist
 * separator ("5.8.1+56057345") and semver build metadata conventionally uses one. That
 * decision was recorded with an obligation attached, in as many words:
 *
 *     "The rule any consumer must follow: percent-encode a build ID before putting it
 *      in a query string."
 *
 * A rule stated only in a comment is a rule that gets forgotten at the first call site.
 * This is that rule, as one function, so the obligation has an implementation rather
 * than a reader.
 *
 * '+' IS THE WHOLE POINT. In an `application/x-www-form-urlencoded` query string -- the
 * form every HTTP stack, every browser and every backend framework parses a query
 * string as -- a literal '+' decodes to a SPACE. So the build ID
 * "dev-0.1.0-5.8.1+56057345-Development-Editor" arrives at a leaderboard as
 * "dev-0.1.0-5.8.1 56057345-Development-Editor": a different string, silently, with no
 * error anywhere. Two builds whose IDs differ only either side of the '+' would collide
 * after that substitution, which is exactly the identity failure FRacingSimBuildId
 * exists to prevent.
 *
 * IN Core/ RATHER THAN Streaming/ OR A FUTURE Http/ LAYER, for the reason CORE-002's
 * M-3 row gives about unit conversions: the rule it enforces is a *Core* contract
 * (which characters a build ID may contain), and putting the encoder next to the first
 * consumer would give that contract a second home the moment a second consumer appears.
 *
 * NOT FGenericPlatformHttp::UrlEncode, deliberately. That lives in the HTTP module, and
 * Core/ takes no module dependency it does not need (RacingSim.Build.cs is Core,
 * CoreUObject, Engine). It also encodes to the same RFC 3986 rule this does, so the
 * dependency would buy nothing.
 *
 * HEADER-ONLY AND INLINE, matching RacingSimUnits.h: it is a pure function over a
 * string, called at most once per result submission, and being inline keeps it usable
 * from anywhere in the module without a link-time thought.
 */
namespace RacingSim::Url
{
	/**
	 * Percent-encode a value for use inside a URL query string, per RFC 3986.
	 *
	 * UNRESERVED SET: `A-Z a-z 0-9 - . _ ~`. Everything else -- including '+', '&',
	 * '=', '#', '@', '/', '?', ' ' and every non-ASCII character -- is encoded as one
	 * or more `%XX` triplets over the value's UTF-8 bytes.
	 *
	 * ENCODED BYTE-WISE OVER UTF-8, not character-wise over TCHAR, because a percent
	 * triplet is defined over octets. Encoding a UTF-16 code unit directly would emit
	 * `%XXXX`, which is not a thing, and would mangle anything outside Latin-1. In
	 * practice every build ID this project produces is pure ASCII
	 * (RacingSim.Core.BuildId asserts `[A-Za-z0-9._+-]`), but a track id, a ruleset id
	 * or a future free-form field is authored text and has no such guarantee.
	 *
	 * DELIBERATELY STRICTER THAN NECESSARY. '.' '_' '-' '~' are left alone because they
	 * are unreserved; nothing else is, even where a particular server would tolerate it.
	 * A query-string encoder that tries to be readable is one that has to be re-audited
	 * every time a value's character set changes.
	 *
	 * This encodes ONE value. Composing `key=value&key=value` is the caller's job, and
	 * the caller must encode each value with this function before joining -- see
	 * FRacingRaceResult::MakeSubmissionQueryString, which is the project's one consumer.
	 */
	inline FString PercentEncodeQueryValue(const FString& In)
	{
		FString Out;

		// Worst case is three characters out per byte in. Reserving it up front keeps
		// this allocation-free after the first append even for a fully encoded value.
		Out.Reserve(In.Len() * 3);

		const FTCHARToUTF8 Utf8(*In);
		const uint8* const Bytes = reinterpret_cast<const uint8*>(Utf8.Get());
		const int32 NumBytes = Utf8.Length();

		for (int32 Index = 0; Index < NumBytes; ++Index)
		{
			const uint8 Byte = Bytes[Index];

			// Compared against ASCII codes rather than FChar::IsAlnum, because this loop
			// walks BYTES: a UTF-8 continuation byte can land in a range a character
			// classifier would answer about as though it were a character, and the answer
			// would be meaningless. The unreserved set is ASCII by definition, so a plain
			// range test is both correct and unambiguous here.
			const bool bUnreserved =
				(Byte >= 'A' && Byte <= 'Z')
				|| (Byte >= 'a' && Byte <= 'z')
				|| (Byte >= '0' && Byte <= '9')
				|| Byte == '-'
				|| Byte == '.'
				|| Byte == '_'
				|| Byte == '~';

			if (bUnreserved)
			{
				Out.AppendChar(static_cast<TCHAR>(Byte));
			}
			else
			{
				// Upper-case hex. RFC 3986 says decoders must accept either case and
				// producers should emit upper; fixing the case also makes the encoded
				// form byte-stable, which matters when it is compared or logged.
				Out.Appendf(TEXT("%%%02X"), static_cast<int32>(Byte));
			}
		}

		return Out;
	}
}
