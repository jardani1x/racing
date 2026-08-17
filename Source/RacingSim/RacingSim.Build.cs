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

		// Comment stripping, in this order: block comments first, then line comments.
		// Without it, the documentation comments this project writes -- which name the
		// banned macros on purpose, to explain the rule -- would fail the build.
		// Crude by design: it does not understand string literals or raw strings. The
		// failure mode is a false positive on a macro name inside a string literal
		// followed by '(', which no plausible code contains. A false *negative* is not
		// reachable this way, and that is the direction that matters.
		Regex BlockComment = new Regex(@"/\*.*?\*/", RegexOptions.Singleline | RegexOptions.Compiled);
		Regex LineComment = new Regex(@"//[^\r\n]*", RegexOptions.Compiled);

		string[] Extensions = { "*.h", "*.hpp", "*.inl", "*.cpp", "*.c" };
		List<string> Violations = new List<string>();

		foreach (string Extension in Extensions)
		{
			foreach (string SourceFile in Directory.EnumerateFiles(ModuleDirectory, Extension, SearchOption.AllDirectories))
			{
				// Any edit to any of these re-runs this constructor. See the comment above.
				ExternalDependencies.Add(SourceFile);

				string Text = File.ReadAllText(SourceFile, Encoding.UTF8);
				string Stripped = LineComment.Replace(BlockComment.Replace(Text, " "), " ");

				Match Found = MacroPattern.Match(Stripped);
				if (Found.Success)
				{
					// Report the line number from the stripped text's prefix, which is
					// line-count-preserving for line comments but not for block
					// comments. Close enough to locate the file; the file name is the
					// actionable part.
					int Line = 1;
					for (int Index = 0; Index < Found.Index; ++Index)
					{
						if (Stripped[Index] == '\n')
						{
							++Line;
						}
					}

					Violations.Add(string.Format(
						"  {0} (around line {1}): {2}",
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
}
