// Copyright RacingSim. All Rights Reserved.

#include "Core/RacingSimValidation.h"

#include "Core/RacingSimLog.h"
#include "UObject/Class.h"
#include "UObject/UnrealType.h"

namespace RacingSim::Validation
{
	namespace Private
	{
#if WITH_METADATA
		// Metadata keys mirrored by FRacingPropertyRange. Deliberately not
		// UIMin/UIMax -- see the header. Guarded because metadata does not exist
		// outside WITH_METADATA and an unused file-scope constant is a warning,
		// which CLAUDE.md does not allow us to suppress.
		static const TCHAR* ClampMinKey = TEXT("ClampMin");
		static const TCHAR* ClampMaxKey = TEXT("ClampMax");
#endif

		/**
		 * Bounds are declared as double; integer properties round them inward so a
		 * fractional bound can never widen an integer's legal range. Values beyond
		 * int64 are saturated rather than wrapped -- an undefined float-to-int
		 * conversion here would be a clamp pass that corrupts the value it is
		 * supposed to be protecting.
		 */
		static int64 FloorToInt64(const double Value)
		{
			const double Floored = FMath::FloorToDouble(Value);
			if (Floored <= static_cast<double>(TNumericLimits<int64>::Lowest()))
			{
				return TNumericLimits<int64>::Lowest();
			}
			if (Floored >= static_cast<double>(TNumericLimits<int64>::Max()))
			{
				return TNumericLimits<int64>::Max();
			}
			return static_cast<int64>(Floored);
		}

		static int64 CeilToInt64(const double Value)
		{
			const double Ceiled = FMath::CeilToDouble(Value);
			if (Ceiled <= static_cast<double>(TNumericLimits<int64>::Lowest()))
			{
				return TNumericLimits<int64>::Lowest();
			}
			if (Ceiled >= static_cast<double>(TNumericLimits<int64>::Max()))
			{
				return TNumericLimits<int64>::Max();
			}
			return static_cast<int64>(Ceiled);
		}

		static FRacingValidationIssue MakeFailure(const FName PropertyName, FString&& Message)
		{
			FRacingValidationIssue Issue;
			Issue.PropertyName = PropertyName;
			Issue.Action = ERangeAction::Failed;
			Issue.Message = MoveTemp(Message);
			return Issue;
		}

		/** Human-readable form of the declared bounds, for messages. "[0, 3]", "[0, +inf)", "(-inf, 240]". */
		static FString DescribeRange(const FRacingPropertyRange& Range)
		{
			const FString Low = Range.bHasMin ? FString::Printf(TEXT("[%g"), Range.Min) : FString(TEXT("(-inf"));
			const FString High = Range.bHasMax ? FString::Printf(TEXT("%g]"), Range.Max) : FString(TEXT("+inf)"));
			return Low + TEXT(", ") + High;
		}

		/**
		 * Resolve one declared range to a numeric property, or explain why not.
		 *
		 * Rejects enum-backed byte properties (a numeric range over an enumerator
		 * is meaningless) and uint64 properties (FNumericProperty's integer
		 * accessors are int64-based, so a uint64 above INT64_MAX would be read as
		 * negative and "corrected" into garbage). Both are range-table bugs, so
		 * they are reported as Failed rather than skipped silently.
		 */
		static FNumericProperty* ResolveNumericProperty(const UClass* Class, const FRacingPropertyRange& Range, FString& OutWhyNot)
		{
			FProperty* Property = Class->FindPropertyByName(Range.PropertyName);
			if (Property == nullptr)
			{
				OutWhyNot = FString::Printf(
					TEXT("%s declares a range for '%s', but that class has no such property. ")
					TEXT("A property was renamed or removed without updating the range table."),
					*Class->GetName(),
					*Range.PropertyName.ToString());
				return nullptr;
			}

			FNumericProperty* Numeric = CastField<FNumericProperty>(Property);
			if (Numeric == nullptr)
			{
				OutWhyNot = FString::Printf(
					TEXT("%s::%s is a %s, not a numeric property; a ClampMin/ClampMax range cannot be applied to it."),
					*Class->GetName(),
					*Range.PropertyName.ToString(),
					*Property->GetClass()->GetName());
				return nullptr;
			}

			if (Numeric->IsEnum())
			{
				OutWhyNot = FString::Printf(
					TEXT("%s::%s is an enum-backed byte property; a numeric range over enumerators is not meaningful."),
					*Class->GetName(),
					*Range.PropertyName.ToString());
				return nullptr;
			}

			if (Numeric->IsA(FUInt64Property::StaticClass()))
			{
				OutWhyNot = FString::Printf(
					TEXT("%s::%s is uint64; FNumericProperty's integer accessors are int64-based, so values above ")
					TEXT("INT64_MAX would be read as negative. Use a narrower integer type or validate it by hand."),
					*Class->GetName(),
					*Range.PropertyName.ToString());
				return nullptr;
			}

			// A static array UPROPERTY (`int32 Foo[4]`) has ArrayDim > 1, and
			// ContainerPtrToValuePtr addresses element 0 only. Validating one
			// element and reporting success would be the same silent half-measure
			// this whole module exists to remove, so refuse instead.
			if (Numeric->ArrayDim > 1)
			{
				OutWhyNot = FString::Printf(
					TEXT("%s::%s is a static array (ArrayDim = %d); this pass addresses element 0 only, so it would ")
					TEXT("validate one element and silently ignore the rest."),
					*Class->GetName(),
					*Range.PropertyName.ToString(),
					Numeric->ArrayDim);
				return nullptr;
			}

			return Numeric;
		}

		/**
		 * The range table is itself declared data, and this module's premise is
		 * that a declared invariant must actually be one. That has to apply to the
		 * table too: an inverted range (Min > Max) is unsatisfiable, and clamping
		 * against it would push every value to one bound and then the other
		 * depending on evaluation order.
		 */
		static bool IsRangeSelfConsistent(const FRacingPropertyRange& Range, const UClass* Class, FString& OutWhyNot)
		{
			if (Range.bHasMin && Range.bHasMax && Range.Min > Range.Max)
			{
				OutWhyNot = FString::Printf(
					TEXT("%s::%s declares an inverted range: minimum %g is greater than maximum %g. No value can satisfy it."),
					*Class->GetName(),
					*Range.PropertyName.ToString(),
					Range.Min,
					Range.Max);
				return false;
			}

			if (Range.bHasReplacement)
			{
				const bool bBelow = Range.bHasMin && Range.ReplacementValue < Range.Min;
				const bool bAbove = Range.bHasMax && Range.ReplacementValue > Range.Max;
				if (bBelow || bAbove)
				{
					OutWhyNot = FString::Printf(
						TEXT("%s::%s declares a replacement value %g that is itself outside its own range."),
						*Class->GetName(),
						*Range.PropertyName.ToString(),
						Range.ReplacementValue);
					return false;
				}
			}

			return true;
		}
	}

	int32 FRacingValidationResult::NumCorrected() const
	{
		int32 Count = 0;
		for (const FRacingValidationIssue& Issue : Issues)
		{
			if (Issue.Action != ERangeAction::Failed && Issue.Action != ERangeAction::InRange)
			{
				++Count;
			}
		}
		return Count;
	}

	int32 FRacingValidationResult::NumFailed() const
	{
		int32 Count = 0;
		for (const FRacingValidationIssue& Issue : Issues)
		{
			if (Issue.Action == ERangeAction::Failed)
			{
				++Count;
			}
		}
		return Count;
	}

	bool FRacingValidationResult::WasCorrected(const FName PropertyName) const
	{
		for (const FRacingValidationIssue& Issue : Issues)
		{
			if (Issue.PropertyName == PropertyName
				&& Issue.Action != ERangeAction::Failed
				&& Issue.Action != ERangeAction::InRange)
			{
				return true;
			}
		}
		return false;
	}

	FString FRacingValidationResult::ToString() const
	{
		TArray<FString> Lines;
		Lines.Reserve(Issues.Num());
		for (const FRacingValidationIssue& Issue : Issues)
		{
			Lines.Add(Issue.Message);
		}
		return FString::Join(Lines, TEXT("\n"));
	}

	FRacingValidationResult EnforceRanges(UObject* Object, TConstArrayView<FRacingPropertyRange> Ranges)
	{
		using namespace Private;

		FRacingValidationResult Result;

		if (Object == nullptr)
		{
			Result.Issues.Add(MakeFailure(NAME_None, TEXT("EnforceRanges was called with a null object.")));
			return Result;
		}

		const UClass* Class = Object->GetClass();

		for (const FRacingPropertyRange& Range : Ranges)
		{
			FString WhyNot;
			if (!IsRangeSelfConsistent(Range, Class, WhyNot))
			{
				Result.Issues.Add(MakeFailure(Range.PropertyName, MoveTemp(WhyNot)));
				continue;
			}

			FNumericProperty* Numeric = ResolveNumericProperty(Class, Range, WhyNot);
			if (Numeric == nullptr)
			{
				Result.Issues.Add(MakeFailure(Range.PropertyName, MoveTemp(WhyNot)));
				continue;
			}

			void* ValuePtr = Numeric->ContainerPtrToValuePtr<uint8>(Object);

			if (Numeric->IsFloatingPoint())
			{
				const double Loaded = Numeric->GetFloatingPointPropertyValue(ValuePtr);
				double Corrected = Loaded;
				ERangeAction Action = ERangeAction::InRange;

				if (!FMath::IsFinite(Loaded))
				{
					// NaN compares false against every bound, so it would pass a
					// naive clamp untouched. There is no nearest legal value to a
					// NaN, so a declared replacement is used if there is one and
					// the range's own safe end otherwise.
					Corrected = Range.bHasReplacement
						? Range.ReplacementValue
						: (Range.bHasMin ? Range.Min : (Range.bHasMax ? Range.Max : 0.0));
					Action = ERangeAction::ReplacedNonFinite;
				}
				else if (Range.bHasMin && Loaded < Range.Min)
				{
					// A declared replacement wins over the bound: for a property
					// like TelemetryStaleAfterSeconds the minimum is a "disabled"
					// sentinel, not its least-permissive value, so clamping to it
					// would disarm a safety guard. See RacingSimValidation.h.
					Corrected = Range.bHasReplacement ? Range.ReplacementValue : Range.Min;
					Action = Range.bHasReplacement ? ERangeAction::ReplacedOutOfRange : ERangeAction::ClampedToMin;
				}
				else if (Range.bHasMax && Loaded > Range.Max)
				{
					Corrected = Range.bHasReplacement ? Range.ReplacementValue : Range.Max;
					Action = Range.bHasReplacement ? ERangeAction::ReplacedOutOfRange : ERangeAction::ClampedToMax;
				}

				if (Action != ERangeAction::InRange)
				{
					Numeric->SetFloatingPointPropertyValue(ValuePtr, Corrected);

					FRacingValidationIssue Issue;
					Issue.PropertyName = Range.PropertyName;
					Issue.Action = Action;
					Issue.LoadedValue = Loaded;
					Issue.CorrectedValue = Corrected;
					Issue.Message = FString::Printf(
						TEXT("%s::%s loaded as %g, which is outside the declared range %s; corrected to %g."),
						*Class->GetName(),
						*Range.PropertyName.ToString(),
						Loaded,
						*DescribeRange(Range),
						Corrected);
					Result.Issues.Add(MoveTemp(Issue));
				}

				continue;
			}

			// Integer path. GetSignedIntPropertyValue widens every supported
			// integer type to int64 losslessly (uint64 was rejected above).
			const int64 Loaded = Numeric->GetSignedIntPropertyValue(ValuePtr);
			int64 Corrected = Loaded;
			ERangeAction Action = ERangeAction::InRange;

			if (Range.bHasMin)
			{
				const int64 MinInt = CeilToInt64(Range.Min);
				if (Loaded < MinInt)
				{
					Corrected = Range.bHasReplacement ? FloorToInt64(Range.ReplacementValue) : MinInt;
					Action = Range.bHasReplacement ? ERangeAction::ReplacedOutOfRange : ERangeAction::ClampedToMin;
				}
			}
			if (Action == ERangeAction::InRange && Range.bHasMax)
			{
				const int64 MaxInt = FloorToInt64(Range.Max);
				if (Loaded > MaxInt)
				{
					Corrected = Range.bHasReplacement ? FloorToInt64(Range.ReplacementValue) : MaxInt;
					Action = Range.bHasReplacement ? ERangeAction::ReplacedOutOfRange : ERangeAction::ClampedToMax;
				}
			}

			if (Action != ERangeAction::InRange)
			{
				Numeric->SetIntPropertyValue(ValuePtr, Corrected);

				FRacingValidationIssue Issue;
				Issue.PropertyName = Range.PropertyName;
				Issue.Action = Action;
				Issue.LoadedValue = static_cast<double>(Loaded);
				Issue.CorrectedValue = static_cast<double>(Corrected);
				Issue.Message = FString::Printf(
					TEXT("%s::%s loaded as %lld, which is outside the declared range %s; corrected to %lld."),
					*Class->GetName(),
					*Range.PropertyName.ToString(),
					Loaded,
					*DescribeRange(Range),
					Corrected);
				Result.Issues.Add(MoveTemp(Issue));
			}
		}

		return Result;
	}

	void LogResult(const FRacingValidationResult& Result, const UObject* Context)
	{
		const FString ContextName = Context != nullptr ? Context->GetClass()->GetName() : FString(TEXT("<null>"));

		for (const FRacingValidationIssue& Issue : Result.Issues)
		{
			if (Issue.Action == ERangeAction::Failed)
			{
				// A Failed issue is a bug in the range table, not bad user data.
				// Error, not Warning: it means a declared invariant is not being
				// enforced at all, which is precisely the silence M-5 was about.
				UE_LOG(LogRacingCore, Error, TEXT("Range validation could not run: %s"), *Issue.Message);
			}
			else if (Issue.Action != ERangeAction::InRange)
			{
				UE_LOG(
					LogRacingCore,
					Warning,
					TEXT("Range validation corrected a configured value: %s Fix the ini or command line that supplied it -- "
						 "the corrected value is what this process will use."),
					*Issue.Message);
			}
		}

		UE_LOG(
			LogRacingCore,
			Verbose,
			TEXT("Range validation on %s: %d corrected, %d could not be applied."),
			*ContextName,
			Result.NumCorrected(),
			Result.NumFailed());
	}

	FRacingValidationResult VerifyRangesMatchMetadata(const UClass* Class, TConstArrayView<FRacingPropertyRange> Ranges)
	{
		using namespace Private;

		FRacingValidationResult Result;

		if (Class == nullptr)
		{
			Result.Issues.Add(MakeFailure(NAME_None, TEXT("VerifyRangesMatchMetadata was called with a null class.")));
			return Result;
		}

#if WITH_METADATA
		// Direction 1: every declared range resolves, and agrees with the metadata.
		for (const FRacingPropertyRange& Range : Ranges)
		{
			FString WhyNot;
			if (!IsRangeSelfConsistent(Range, Class, WhyNot))
			{
				Result.Issues.Add(MakeFailure(Range.PropertyName, MoveTemp(WhyNot)));
				continue;
			}

			const FNumericProperty* Numeric = ResolveNumericProperty(Class, Range, WhyNot);
			if (Numeric == nullptr)
			{
				Result.Issues.Add(MakeFailure(Range.PropertyName, MoveTemp(WhyNot)));
				continue;
			}

			const bool bMetaHasMin = Numeric->HasMetaData(ClampMinKey);
			const bool bMetaHasMax = Numeric->HasMetaData(ClampMaxKey);

			if (bMetaHasMin != Range.bHasMin)
			{
				Result.Issues.Add(MakeFailure(
					Range.PropertyName,
					FString::Printf(
						TEXT("%s::%s: the range table %s a minimum but the UPROPERTY metadata %s ClampMin. ")
						TEXT("The Details panel and the config-load clamp would disagree."),
						*Class->GetName(),
						*Range.PropertyName.ToString(),
						Range.bHasMin ? TEXT("declares") : TEXT("omits"),
						bMetaHasMin ? TEXT("declares") : TEXT("omits"))));
			}
			else if (bMetaHasMin)
			{
				const double MetaMin = FCString::Atod(*Numeric->GetMetaData(ClampMinKey));
				if (MetaMin != Range.Min)
				{
					Result.Issues.Add(MakeFailure(
						Range.PropertyName,
						FString::Printf(
							TEXT("%s::%s: range table minimum %g does not match UPROPERTY ClampMin \"%s\"."),
							*Class->GetName(),
							*Range.PropertyName.ToString(),
							Range.Min,
							*Numeric->GetMetaData(ClampMinKey))));
				}
			}

			if (bMetaHasMax != Range.bHasMax)
			{
				Result.Issues.Add(MakeFailure(
					Range.PropertyName,
					FString::Printf(
						TEXT("%s::%s: the range table %s a maximum but the UPROPERTY metadata %s ClampMax. ")
						TEXT("The Details panel and the config-load clamp would disagree."),
						*Class->GetName(),
						*Range.PropertyName.ToString(),
						Range.bHasMax ? TEXT("declares") : TEXT("omits"),
						bMetaHasMax ? TEXT("declares") : TEXT("omits"))));
			}
			else if (bMetaHasMax)
			{
				const double MetaMax = FCString::Atod(*Numeric->GetMetaData(ClampMaxKey));
				if (MetaMax != Range.Max)
				{
					Result.Issues.Add(MakeFailure(
						Range.PropertyName,
						FString::Printf(
							TEXT("%s::%s: range table maximum %g does not match UPROPERTY ClampMax \"%s\"."),
							*Class->GetName(),
							*Range.PropertyName.ToString(),
							Range.Max,
							*Numeric->GetMetaData(ClampMaxKey))));
				}
			}
		}

		// Direction 2 -- the one that catches the regression. Any config-backed
		// numeric property carrying a clamp in its metadata must be in the table,
		// or its range is enforced only in the Details panel: exactly the M-5 shape.
		for (TFieldIterator<FProperty> It(Class); It; ++It)
		{
			const FProperty* Property = *It;
			if (!Property->HasAnyPropertyFlags(CPF_Config))
			{
				continue;
			}

			const FNumericProperty* Numeric = CastField<FNumericProperty>(Property);
			if (Numeric == nullptr || Numeric->IsEnum())
			{
				continue;
			}

			if (!Numeric->HasMetaData(ClampMinKey) && !Numeric->HasMetaData(ClampMaxKey))
			{
				continue;
			}

			const FName PropertyName = Property->GetFName();
			const bool bDeclared = Ranges.ContainsByPredicate(
				[PropertyName](const FRacingPropertyRange& Range) { return Range.PropertyName == PropertyName; });

			if (!bDeclared)
			{
				Result.Issues.Add(MakeFailure(
					PropertyName,
					FString::Printf(
						TEXT("%s::%s is a config property with ClampMin/ClampMax metadata but is absent from the range table, ")
						TEXT("so its range constrains the Details panel only and an -ini: override bypasses it (CORE-002 M-5). ")
						TEXT("Add it to the class's range table."),
						*Class->GetName(),
						*PropertyName.ToString())));
			}
		}
#else
		// No metadata exists in this configuration; there is nothing to compare
		// against. Silence here is correct and is why the range table, not the
		// metadata, is the runtime authority. See the header.
		(void)Ranges;
#endif // WITH_METADATA

		return Result;
	}
}
