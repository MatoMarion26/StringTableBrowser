// Copyright (c) 2026 Mato Marion. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Internationalization/Regex.h"
#include "StringTableBrowserModule.h"

/**
 * FStringTableSearchFilter
 *
 * Self-contained, reusable search state shared between SStringTableBrowser and
 * SStringTablePickerDropdown. Centralising this logic guarantees both widgets
 * behave identically without duplicating code.
 *
 * Typical usage:
 *   1. Store an instance as a member of the widget.
 *   2. Call Compile() after any state change (search text or toggle).
 *   3. Call PassesFilter() once per cache entry inside the filter loop.
 *
 * Thread safety: this struct is not thread-safe. Always call from the game thread.
 */
struct FStringTableSearchFilter
{
	// -------------------------------------------------------------------------
	// Public state — set these then call Compile()
	// -------------------------------------------------------------------------

	/** The raw text the user has typed into the search box. */
	FString SearchText;

	// Match modes (mutually exclusive for Regex / WholeWord; MatchCase combines with either)
	bool bMatchCase  = false;
	bool bWholeWord  = false;
	bool bRegex      = false;

	// Search scope — determines which entry fields are included in the search target.
	// If all three are false, every entry passes (behaviour: show all).
	bool bSearchKeys   = false;
	bool bSearchValues = true;   // Values only by default
	bool bSearchTables = false;

	// -------------------------------------------------------------------------
	// Compile — call once after any state change, before iterating with PassesFilter
	// -------------------------------------------------------------------------

	/**
	 * Builds and validates the regex pattern from the current state.
	 * Must be called after any change to SearchText, bMatchCase, bWholeWord, or bRegex.
	 * Logs a warning and marks the pattern invalid if the regex cannot be compiled.
	 */
	void Compile()
	{
		CompiledPattern.Reset();
		bPatternValid = false;

		// Plain-text mode has no pattern to compile
		if (SearchText.IsEmpty() || (!bRegex && !bWholeWord))
		{
			return;
		}

		// Prefix (?i) for case-insensitive matching unless Match Case is on
		const FString CasePrefix = bMatchCase ? TEXT("") : TEXT("(?i)");

		FString PatternString;
		if (bWholeWord)
		{
			// Whole Word: wrap the escaped search term in word-boundary anchors.
			// EscapeRegex() replaces FRegexMatcher::Sanitize which has no public API contract.
			PatternString = FString::Printf(TEXT("%s\\b%s\\b"), *CasePrefix, *EscapeRegex(SearchText));
		}
		else
		{
			// Regex mode: use the raw user pattern with the case prefix prepended
			PatternString = FString::Printf(TEXT("%s%s"), *CasePrefix, *SearchText);
		}

		// Validate by constructing and exercising the pattern.
		// FRegexPattern does not expose IsValid(), so try/catch is the safest guard.
		try
		{
			FRegexPattern TestPattern(PatternString);
			FRegexMatcher TestMatcher(TestPattern, TEXT(""));
			TestMatcher.FindNext();

			bPatternValid    = true;
			CompiledPattern  = MoveTemp(TestPattern);
		}
		catch (...)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("StringTableBrowser: Invalid search pattern — \"%s\""), *PatternString);
		}
	}

	// -------------------------------------------------------------------------
	// PassesFilter — call once per entry after Compile()
	// -------------------------------------------------------------------------

	/**
	 * Returns true if the entry should be visible given the current filter state.
	 * An empty SearchText always returns false (caller decides the empty-state behaviour).
	 * If no scope field is selected, all entries pass.
	 */
	bool PassesFilter(const TSharedPtr<FStringTableBrowserEntry>& Item) const
	{
		if (SearchText.IsEmpty())
		{
			return false;
		}

		// No scope selected → treat as unfiltered
		if (!bSearchKeys && !bSearchValues && !bSearchTables)
		{
			return true;
		}

		// Build the search target from only the active scope fields.
		// Fields are space-separated so a phrase cannot accidentally span two fields.
		FString SearchTarget;
		auto AppendField = [&SearchTarget](const FString& Field)
		{
			if (!SearchTarget.IsEmpty())
			{
				SearchTarget.AppendChar(TEXT(' '));
			}
			SearchTarget.Append(Field);
		};

		if (bSearchKeys)   { AppendField(Item->Key);              }
		if (bSearchValues) { AppendField(Item->Value);            }
		if (bSearchTables) { AppendField(Item->TableId.ToString()); }

		if (bRegex || bWholeWord)
		{
			if (!bPatternValid || !CompiledPattern.IsSet())
			{
				return false;
			}
			FRegexMatcher Matcher(CompiledPattern.GetValue(), SearchTarget);
			return Matcher.FindNext();
		}

		const ESearchCase::Type SearchCase = bMatchCase
			? ESearchCase::CaseSensitive
			: ESearchCase::IgnoreCase;

		return SearchTarget.Contains(SearchText, SearchCase);
	}

	// -------------------------------------------------------------------------
	// Static helpers
	// -------------------------------------------------------------------------

	/**
	 * Escapes all ICU regex metacharacters in a raw string.
	 * Used instead of the internal FRegexMatcher::Sanitize (no public API contract).
	 */
	static FString EscapeRegex(const FString& InRaw)
	{
		static const TCHAR MetaChars[] = TEXT("\\.+*?[^]$(){}=!<>|:-#");

		FString Result;
		Result.Reserve(InRaw.Len() * 2);

		for (const TCHAR Ch : InRaw)
		{
			if (FCString::Strchr(MetaChars, Ch) != nullptr)
			{
				Result.AppendChar(TEXT('\\'));
			}
			Result.AppendChar(Ch);
		}

		return Result;
	}

private:
	/** Compiled pattern, valid only when bPatternValid is true. */
	TOptional<FRegexPattern> CompiledPattern;

	/** Set to true by Compile() only when the pattern was constructed without error. */
	bool bPatternValid = false;
};
