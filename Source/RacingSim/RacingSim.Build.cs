// Copyright RacingSim. All Rights Reserved.

using System.Collections.Generic;
using System.IO;
using System.Text;
using System.Text.RegularExpressions;
using UnrealBuildTool;

public class RacingSim : ModuleRules
{
	/**
	 * TEST-001 / CORE-001 finding N-2.
	 *
	 * Every public automation-test-declaring macro in
	 * Engine/Source/Runtime/Core/Public/Misc/AutomationTest.h (UE 5.8.1, lines
	 * 4297-4384). The list is deliberately the *declaring* macros only -- the ones
	 * that call FAutomationTestFramework::RegisterAutomationTest and therefore put a
	 * test into the shipped binary. UTEST_* assertion macros and
	 * ADD_LATENT_AUTOMATION_COMMAND are not on the list: they cannot register a test
	 * on their own, and banning them would flag helper code that legitimately never
	 * ships a test.
	 *
	 * DEFINE_LATENT_AUTOMATION_COMMAND* is included even though it registers nothing,
	 * because a latent command is test scaffolding by definition and has no reason to
	 * exist in a runtime module.
	 */
	private static readonly string[] BannedAutomationMacros =
	{
		"IMPLEMENT_SIMPLE_AUTOMATION_TEST",
		"IMPLEMENT_COMPLEX_AUTOMATION_TEST",
		"IMPLEMENT_COMPLEX_AUTOMATION_CLASS",
		"IMPLEMENT_NETWORKED_AUTOMATION_TEST",
		"IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST",
		"IMPLEMENT_CUSTOM_COMPLEX_AUTOMATION_TEST",
		"IMPLEMENT_BDD_AUTOMATION_TEST",
		"IMPLEMENT_SIMPLE_AUTOMATION_TEST_PRIVATE",
		"IMPLEMENT_COMPLEX_AUTOMATION_TEST_PRIVATE",
		"IMPLEMENT_NETWORKED_AUTOMATION_TEST_PRIVATE",
		"IMPLEMENT_BDD_AUTOMATION_TEST_PRIVATE",
		"DEFINE_SPEC",
		"DEFINE_SPEC_PRIVATE",
		"BEGIN_DEFINE_SPEC",
		"BEGIN_DEFINE_SPEC_PRIVATE",
		"REGISTER_SIMPLE_AUTOMATION_TEST_TAGS",
		"DEFINE_LATENT_AUTOMATION_COMMAND",
		"DEFINE_EXPORTED_LATENT_AUTOMATION_COMMAND",
		"DEFINE_ENGINE_LATENT_AUTOMATION_COMMAND"
	};

	public RacingSim(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// TEST-001, N-2. Runs on every target that builds this module, so both
		// RacingSimEditor and RacingSim (Game) enforce it. See the method comment for
		// what this does and does not catch.
		EnforceNoAutomationTestsInRuntimeModule();

		// Required for the layer-folder layout to compile.
		//
		// DefaultBuildSettings = V7 sets bLegacyPublicIncludePaths = false, so UBT
		// does not add the module root to the include path -- only a Public/ folder,
		// which this module deliberately does not have. Without this line,
		// #include "Core/RacingSimLog.h" fails with C1083 even though the file is
		// right there. (It cost one failed build to learn; RacingSim.h resolved fine
		// only because it sits at the module root.)
		//
		// The alternative was a Public/Private split with the layers nested inside.
		// This keeps the flat layer layout that Docs/15-ProjectStructure.md, README.md
		// and Docs/Tickets.md all specify. Exported publicly because RacingSimTests
		// includes layer headers.
		PublicIncludePaths.Add(ModuleDirectory);

		// CORE-001 delivered the layer layout: Core/, Vehicle/, Race/, UI/ and
		// Streaming/ are folders in this module, not separate modules (decision
		// 2026-08-12, Docs/15-ProjectStructure.md). Tests live in the separate
		// UncookedOnly RacingSimTests module so they cannot ship.
		//
		// Dependencies stay minimal deliberately. Add one only when a layer actually
		// needs it -- ChaosVehicles at VEH-002, UMG at UI-001, PixelStreaming2 at
		// STREAM-001 -- so the dependency list stays evidence of what is really used.
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",

			// CORE-002: URacingSimSettings derives from UDeveloperSettings, which
			// lives in the DeveloperSettings module (not in Engine). Public rather
			// than private because Core/RacingSimSettings.h exposes the base class
			// to every dependent, including RacingSimTests.
			"DeveloperSettings"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });
	}

	/**
	 * Fails the build if an automation-test-declaring macro appears anywhere under
	 * Source/RacingSim/.
	 *
	 * ---------------------------------------------------------------------------
	 * Why this exists (CORE-001 finding N-2)
	 * ---------------------------------------------------------------------------
	 *
	 * CORE-001 claimed "test code physically cannot ship" on the strength of
	 * RacingSimTests being an UncookedOnly module. That claim is true only of code
	 * *in that module*. WITH_DEV_AUTOMATION_TESTS is 1 in a Development Game target
	 * (ObjectMacros / UEBuildTarget defaults), so an IMPLEMENT_SIMPLE_AUTOMATION_TEST
	 * written inside Source/RacingSim/ compiles straight into RacingSim.exe, and the
	 * .target-receipt and manifest checks CORE-001 used would all still pass -- they
	 * check which *modules* were compiled, and this module is supposed to be there.
	 *
	 * So the module boundary is not self-enforcing, and this scan is what makes the
	 * rule mechanical instead of a review convention.
	 *
	 * ---------------------------------------------------------------------------
	 * Staleness, and why ExternalDependencies is load-bearing
	 * ---------------------------------------------------------------------------
	 *
	 * UBT caches its makefile. A ModuleRules constructor does not re-run on every
	 * invocation, so a naive scan here would go stale: adding a banned macro to an
	 * *existing* file recompiles that file but would not re-run this check.
	 *
	 * ModuleRules.ExternalDependencies is "external files which invalidate the
	 * makefile if modified" (ModuleRules.cs:1437-1439), consumed at
	 * UEBuildTarget.cs:3460-3464. Registering every scanned file there means any edit
	 * to any file in this module invalidates the makefile and re-runs this scan.
	 * Without those lines the check degrades into a one-time result -- exactly the
	 * decay mode CORE-001's reviewer warned about.
	 *
	 * ---------------------------------------------------------------------------
	 * What this does NOT catch
	 * ---------------------------------------------------------------------------
	 *
	 * - A test registered without a macro, by deriving from FAutomationTestBase and
	 *   constructing it directly. That is caught at runtime by
	 *   RacingSim.Tests.AutomationTestPlacement, which inspects the framework's own
	 *   registry rather than source text.
	 * - A macro assembled by token pasting or reached through another header.
	 * - Tests in a future third module. Add that module here if one appears.
	 *
	 * The two checks are deliberately different in kind (source text at build time,
	 * live registry at run time) so that neither one's blind spot is shared.
	 */
	private void EnforceNoAutomationTestsInRuntimeModule()
	{
		// Order longest-first so IMPLEMENT_SIMPLE_AUTOMATION_TEST_PRIVATE is reported
		// as itself rather than as its shorter prefix. \b on both sides prevents the
		// shorter name matching inside the longer one in the first place; the sort is
		// belt-and-braces for readability of the error message.
		List<string> Macros = new List<string>(BannedAutomationMacros);
		Macros.Sort((A, B) => B.Length.CompareTo(A.Length));

		// Token, optional whitespace, then '('. Requiring the paren means a mention of
		// the bare macro name in prose (this comment block, for one) is not a match.
		Regex MacroPattern = new Regex(
			@"\b(" + string.Join("|", Macros) + @")\s*\(",
			RegexOptions.Compiled);

		string[] Extensions = { "*.h", "*.hpp", "*.inl", "*.cpp", "*.c" };
		List<string> Violations = new List<string>();

		foreach (string Extension in Extensions)
		{
			foreach (string SourceFile in Directory.EnumerateFiles(ModuleDirectory, Extension, SearchOption.AllDirectories))
			{
				// Any edit to any of these re-runs this constructor. See the comment above.
				ExternalDependencies.Add(SourceFile);

				string Text = File.ReadAllText(SourceFile, Encoding.UTF8);
				string Stripped = BlankCommentsAndLiterals(Text);

				Match Found = MacroPattern.Match(Stripped);
				if (Found.Success)
				{
					// BlankCommentsAndLiterals preserves length and newlines, so an
					// index into the stripped text is the same index in the original
					// and the line number is exact rather than approximate.
					int Line = 1;
					for (int Index = 0; Index < Found.Index; ++Index)
					{
						if (Stripped[Index] == '\n')
						{
							++Line;
						}
					}

					Violations.Add(string.Format(
						"  {0} (line {1}): {2}",
						SourceFile, Line, Found.Groups[1].Value));
				}
			}
		}

		if (Violations.Count > 0)
		{
			throw new BuildException(
				"RacingSim: automation-test macros are not permitted in the runtime module.\n" +
				"Docs/01-Architecture.md 'Tests' and Docs/15-ProjectStructure.md require every\n" +
				"automation test to live under Source/RacingSimTests/, which is UncookedOnly and\n" +
				"therefore cannot be built into a packaged Game target. WITH_DEV_AUTOMATION_TESTS\n" +
				"is 1 in a Development Game build, so a test declared here would ship.\n" +
				"Move these to Source/RacingSimTests/:\n" +
				string.Join("\n", Violations));
		}
	}

	/**
	 * Blanks the *contents* of comments, string literals and character literals,
	 * replacing each consumed character with a space and leaving newlines in place.
	 *
	 * ---------------------------------------------------------------------------
	 * Why this is a scanner and not two regexes (TEST-001 repair cycle 1, T-2)
	 * ---------------------------------------------------------------------------
	 *
	 * The previous implementation was:
	 *
	 *     BlockComment = new Regex(@"/\*.*?\*\/", RegexOptions.Singleline);
	 *     LineComment  = new Regex(@"//[^\r\n]*");
	 *     Stripped = LineComment.Replace(BlockComment.Replace(Text, " "), " ");
	 *
	 * and carried a comment claiming "a false negative is not reachable this way, and
	 * that is the direction that matters". That claim was wrong, and a false negative
	 * is the direction that matters, so it is worth being precise about how it failed.
	 *
	 * Block comments were stripped FIRST and the pattern is Singleline, so `.` matches
	 * newlines. Any `/*` appearing where C++ does not treat it as opening a comment --
	 * inside a `//` line comment, or inside a string literal -- still opened a match
	 * for the regex, which then ran to the next `*\/` ANYWHERE LATER IN THE FILE and
	 * blanked everything in between. Real code in that span was silently discarded
	 * before the macro search ever saw it. Concretely, this passed the old scan:
	 *
	 *     // TODO: the /* form is deprecated
	 *     IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShipped, "X", Flags)
	 *     /* any later block comment closes the runaway match *\/
	 *
	 * That is a banned macro compiled into the runtime module with a green build --
	 * exactly the regression N-2 exists to prevent, hidden by the check meant to catch
	 * it. A one-line comment is enough to disable the gate, which also makes it a
	 * plausible accident rather than only a deliberate bypass.
	 *
	 * A single left-to-right pass fixes it because it models what the C++ lexer
	 * actually does: whichever of `//`, `/*`, `"` or `'` is encountered first in
	 * ordinary code wins, and the others are inert until it closes. Handling string
	 * literals in the same pass removes the old false-positive caveat too -- a macro
	 * name inside a string literal is no longer a match.
	 *
	 * Length and line structure are preserved so that a match index in the returned
	 * text is a valid index into the original, which is what makes the reported line
	 * number exact instead of "around line N".
	 *
	 * Remaining limits, stated rather than implied: preprocessor conditionals are not
	 * evaluated, so a macro inside `#if 0` is still reported (false positive, safe
	 * direction), and trigraphs are not handled (removed in C++17).
	 */
	private static string BlankCommentsAndLiterals(string Text)
	{
		char[] Out = Text.ToCharArray();
		int Length = Text.Length;

		// Blank one character in place, keeping newlines so line numbers stay exact.
		System.Action<int> Blank = Index =>
		{
			if (Out[Index] != '\n' && Out[Index] != '\r')
			{
				Out[Index] = ' ';
			}
		};

		int i = 0;
		while (i < Length)
		{
			char C = Text[i];

			// Line comment: // to end of line, honouring backslash line-continuation.
			if (C == '/' && i + 1 < Length && Text[i + 1] == '/')
			{
				while (i < Length && Text[i] != '\n')
				{
					if (Text[i] == '\\')
					{
						// A backslash immediately before the line break continues the
						// comment onto the next line. Consume through that break.
						int j = i + 1;
						while (j < Length && Text[j] == '\r')
						{
							++j;
						}
						if (j < Length && Text[j] == '\n')
						{
							while (i <= j)
							{
								Blank(i);
								++i;
							}
							continue;
						}
					}
					Blank(i);
					++i;
				}
				continue;
			}

			// Block comment: /* ... */. Does not nest in C++.
			if (C == '/' && i + 1 < Length && Text[i + 1] == '*')
			{
				Blank(i); ++i;
				Blank(i); ++i;
				while (i < Length)
				{
					if (Text[i] == '*' && i + 1 < Length && Text[i + 1] == '/')
					{
						Blank(i); ++i;
						Blank(i); ++i;
						break;
					}
					Blank(i);
					++i;
				}
				continue;
			}

			// Raw string literal: R"delim( ... )delim". Handled explicitly because
			// escapes do not apply inside one and it may span lines legally.
			if (C == 'R' && i + 1 < Length && Text[i + 1] == '"')
			{
				int DelimStart = i + 2;
				int DelimEnd = DelimStart;
				// The delimiter is at most 16 characters and cannot contain '(' or '"'.
				while (DelimEnd < Length
					&& Text[DelimEnd] != '('
					&& Text[DelimEnd] != '"'
					&& DelimEnd - DelimStart <= 16)
				{
					++DelimEnd;
				}
				if (DelimEnd < Length && Text[DelimEnd] == '(')
				{
					string Terminator = ")" + Text.Substring(DelimStart, DelimEnd - DelimStart) + "\"";
					int End = Text.IndexOf(Terminator, DelimEnd, System.StringComparison.Ordinal);
					int Stop = (End < 0) ? Length : End + Terminator.Length;
					while (i < Stop)
					{
						Blank(i);
						++i;
					}
					continue;
				}
				// Not actually a raw string; fall through and treat 'R' as code.
			}

			// String or character literal.
			if (C == '"' || C == '\'')
			{
				// A single quote directly after an alphanumeric is a C++14 digit
				// separator (1'000'000), not a character literal. Misreading it would
				// blank real code to the end of the line.
				if (C == '\'' && i > 0)
				{
					char Prev = Text[i - 1];
					if (char.IsLetterOrDigit(Prev) || Prev == '_')
					{
						++i;
						continue;
					}
				}

				char Quote = C;
				Blank(i); ++i;
				while (i < Length)
				{
					if (Text[i] == '\\' && i + 1 < Length)
					{
						Blank(i); ++i;
						Blank(i); ++i;
						continue;
					}
					if (Text[i] == Quote)
					{
						Blank(i); ++i;
						break;
					}
					// An unterminated literal must not swallow the rest of the file --
					// that is the same runaway failure this method exists to remove.
					if (Text[i] == '\n')
					{
						break;
					}
					Blank(i);
					++i;
				}
				continue;
			}

			++i;
		}

		return new string(Out);
	}
}
