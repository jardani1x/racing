// Copyright RacingSim. All Rights Reserved.

#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Vehicle/RacingVehiclePawn.h"

/**
 * VEH-012 / MEDIUM-6: ARacingVehiclePawn's chassis wake threshold used to be a
 * hand-copied 0.02f sitting next to a comment saying it matched
 * FVehicleDebugParams::ControlInputWakeTolerance (ChaosVehicleMovementComponent.h:53).
 * Nothing detected drift: if Epic changed that default in a later engine patch, our copy
 * would go on being 0.02f and nothing would say so.
 *
 * The pawn now reads the engine's own value through the console variable
 * p.Vehicle.ControlInputWakeTolerance (registered at
 * ChaosVehicleMovementComponent.cpp:77). The struct itself cannot be read directly:
 * GVehicleDebugParams is defined at ChaosVehicleMovementComponent.cpp:58 with no
 * CHAOSVEHICLES_API, and every extern for it lives in that module's own Private sources
 * (ChaosVehicleManager.cpp:24, ChaosVehicleManagerAsyncCallback.cpp:10), so RacingSim
 * cannot link it. The console registry is the only supported reader.
 *
 * That leaves two ways to be wrong, and this suite covers both:
 *
 *   - The pawn stops following the engine (the variable disappears or is not read), which
 *     the live-change cases catch.
 *   - The engine's default moves away from ours, which the drift case catches. It is
 *     compared against GetDefaultValue() -- the value the variable was CONSTRUCTED with
 *     -- and not against GetFloat(), so a local ini or a console override cannot make the
 *     check pass or fail spuriously.
 *
 * Pure: no actor, no UWorld. GetChassisWakeInputTolerance() is static precisely so this
 * can run at the Smoke gate.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRacingSimVehicleChassisWakeToleranceTest,
	"RacingSim.Vehicle.ChassisWakeToleranceTracksEngineCvar",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FRacingSimVehicleChassisWakeToleranceTest::RunTest(const FString& Parameters)
{
	IConsoleVariable* const ToleranceCVar =
		IConsoleManager::Get().FindConsoleVariable(ARacingVehiclePawn::ChassisWakeInputToleranceCVarName);

	// Everything below reads or writes this variable, so a missing one is fatal to the
	// suite rather than one failed assertion among many. It also IS the failure MEDIUM-6
	// asked for: if ChaosVehicles stops registering it, the pawn has silently fallen back
	// to a compiled constant and no longer tracks the engine at all.
	if (!TestNotNull(
			FString::Printf(TEXT("the engine registers %s (ChaosVehicleMovementComponent.cpp:77)"),
				ARacingVehiclePawn::ChassisWakeInputToleranceCVarName),
			ToleranceCVar))
	{
		return false;
	}

	// Restored on every exit path, including a failed assertion below. Unset first, not
	// Set: restoring the VALUE is not the same as restoring the VARIABLE. SetByConsole is
	// the maximum priority (ECVF_SetByConsole == 0x10000000, IConsoleManager.h:187), so
	// this test's writes leave a priority residue that outlives them -- for the rest of
	// the process any later Set at Code, Commandline, DeviceProfile, Scalability or
	// SystemSettingsIni priority would be dropped without a word. Unset drops this test's
	// entry from the priority history, and the previously winning setter owns the
	// variable again with its own value.
	//
	// That alone restores anything set by an ini, the command line or code. It does not
	// restore a value a human typed into the console, because the history keeps one slot
	// per priority and this test's writes overwrote that slot. The re-Set below covers
	// exactly that case, and only that case: it fires when the fallback value does not
	// match what was there on entry, which is also the only case where re-establishing a
	// SetByConsole entry is the correct thing to do.
	const FString PriorValue = ToleranceCVar->GetString();
	ON_SCOPE_EXIT
	{
		ToleranceCVar->Unset(ECVF_SetByConsole);
		if (ToleranceCVar->GetString() != PriorValue)
		{
			ToleranceCVar->Set(*PriorValue, ECVF_SetByConsole);
		}
	};

	// -- Drift: the engine default versus our fallback. --
	// GetDefaultValue(), not GetFloat(): the construction-time value, immune to any ini or
	// console override active on this machine. That immunity rests on one premise worth
	// stating, because it is about ChaosVehicles and not about this file: nothing writes
	// GVehicleDebugParams.ControlInputWakeTolerance through the struct. Every path the
	// engine offers -- ini, command line, console, device profile -- goes through the
	// variable's Set, which writes a lower-priority history slot and leaves the
	// SetByConstructor slot holding the compiled value. A direct struct assignment
	// somewhere in ChaosVehicles would move the effective threshold without moving the
	// default, and this assertion would not see it. When this fails, Epic moved
	// FVehicleDebugParams::ControlInputWakeTolerance and
	// ARacingVehiclePawn::ChassisWakeInputToleranceFallback must be moved to match it --
	// after checking that the new value is still right for this pawn's wider predicate,
	// which deliberately differs from Chaos's (see WakeChassisForInput).
	{
		const FString DefaultString = ToleranceCVar->GetDefaultValue();
		const float EngineDefault = FCString::Atof(*DefaultString);
		AddInfo(FString::Printf(TEXT("%s default='%s' (%f); pawn fallback=%f"),
			ARacingVehiclePawn::ChassisWakeInputToleranceCVarName,
			*DefaultString, EngineDefault, ARacingVehiclePawn::ChassisWakeInputToleranceFallback));
		TestTrue(TEXT("the engine's registered default parses to a positive, finite number"),
			FMath::IsFinite(EngineDefault) && EngineDefault > 0.0f);
		TestNearlyEqual(
			TEXT("ChassisWakeInputToleranceFallback still equals the engine's own default for p.Vehicle.ControlInputWakeTolerance"),
			EngineDefault, ARacingVehiclePawn::ChassisWakeInputToleranceFallback, 1.0e-6f);
	}

	// -- Live: the accessor reads the variable, it does not copy it once. --
	// 0.25f is far from any plausible default, so a pass cannot be an accident of the
	// two numbers happening to agree.
	{
		ToleranceCVar->Set(TEXT("0.25"), ECVF_SetByConsole);
		TestNearlyEqual(TEXT("the accessor returns a value set on the console variable"),
			ARacingVehiclePawn::GetChassisWakeInputTolerance(), 0.25f, 1.0e-6f);

		ToleranceCVar->Set(TEXT("0.0125"), ECVF_SetByConsole);
		TestNearlyEqual(TEXT("the accessor follows a SECOND change, so nothing caches the float"),
			ARacingVehiclePawn::GetChassisWakeInputTolerance(), 0.0125f, 1.0e-6f);
	}

	// -- Refusal: a threshold that would defeat the sleep policy is not obeyed. --
	// At zero or below, every axis compares >= the threshold, so every frame would read as
	// "the driver is asking for motion" and the pawn would wake the chassis forever. The
	// accessor falls back instead of forwarding it.
	//
	// Each write is confirmed on the variable before the accessor is asked. A refused Set
	// is silent -- IConsoleVariable::Set returns void -- so without this the pair "the
	// hostile value never landed" and "the accessor refused the hostile value" would be
	// indistinguishable, and the refusal could be credited to the accessor when the
	// variable had simply kept a safe value all along.
	{
		ToleranceCVar->Set(TEXT("0.0"), ECVF_SetByConsole);
		TestEqual(TEXT("the zero write actually landed on the console variable"),
			ToleranceCVar->GetFloat(), 0.0f);
		TestNearlyEqual(TEXT("a zero threshold falls back rather than making every frame count as input"),
			ARacingVehiclePawn::GetChassisWakeInputTolerance(),
			ARacingVehiclePawn::ChassisWakeInputToleranceFallback, 1.0e-6f);

		ToleranceCVar->Set(TEXT("-1.0"), ECVF_SetByConsole);
		TestEqual(TEXT("the negative write actually landed on the console variable"),
			ToleranceCVar->GetFloat(), -1.0f);
		TestNearlyEqual(TEXT("a negative threshold falls back"),
			ARacingVehiclePawn::GetChassisWakeInputTolerance(),
			ARacingVehiclePawn::ChassisWakeInputToleranceFallback, 1.0e-6f);
	}

	return true;
}
