// MmdScopedUtf8CType.cpp
#include "MmdScopedUtf8CType.h"

#include <locale.h>
#include <wctype.h>
#if PLATFORM_MAC
#include <xlocale.h>
#endif

DEFINE_LOG_CATEGORY_STATIC(LogMmdLocale, Log, All);

bool FMmdScopedUtf8CType::IsJapaneseAlphaOnThisThread()
{
	// あ(U+3042) / ア(U+30A2) / 漢(U+6F22)
	return iswalpha((wint_t)0x3042) != 0
		&& iswalpha((wint_t)0x30A2) != 0
		&& iswalpha((wint_t)0x6F22) != 0;
}

FMmdScopedUtf8CType::FMmdScopedUtf8CType()
{
#if PLATFORM_WINDOWS
	// このスレッドだけロケールを持てるようにする(戻り値は元の設定)
	PrevThreadConfig = _configthreadlocale(_ENABLE_PER_THREAD_LOCALE);
	if (PrevThreadConfig == -1)
	{
		UE_LOG(LogMmdLocale, Warning, TEXT("_configthreadlocale failed; locale not switched."));
		return;
	}

	const char* Cur = setlocale(LC_CTYPE, nullptr);
	PrevLocale = Cur ? FString(ANSI_TO_TCHAR(Cur)) : FString(TEXT("C"));

	static const char* Candidates[] = { ".UTF-8", "ja-JP" };
	for (const char* Name : Candidates)
	{
		if (setlocale(LC_CTYPE, Name) != nullptr)
		{
			bActive = true;
			AppliedLocale = ANSI_TO_TCHAR(Name);
			break;
		}
	}
	if (!bActive)
	{
		_configthreadlocale(PrevThreadConfig);
		PrevThreadConfig = -1;
	}
#else
	// C.UTF-8 は Linux 向け、UTF-8 は macOS 向けの中立な候補
	static const char* Candidates[] = { "C.UTF-8", "UTF-8", "ja_JP.UTF-8", "en_US.UTF-8" };
	for (const char* Name : Candidates)
	{
		locale_t L = newlocale(LC_CTYPE_MASK, Name, (locale_t)0);
		if (L != (locale_t)0)
		{
			NewLoc = (void*)L;
			OldLoc = (void*)uselocale(L);
			bActive = true;
			AppliedLocale = ANSI_TO_TCHAR(Name);
			break;
		}
	}
#endif

	if (!bActive)
	{
		UE_LOG(LogMmdLocale, Warning, TEXT("No UTF-8 LC_CTYPE available; Japanese names may be sanitized to '_'."));
	}
	else if (!IsJapaneseAlphaOnThisThread())
	{
		UE_LOG(LogMmdLocale, Warning, TEXT("Locale '%s' applied, but iswalpha still rejects Japanese."), *AppliedLocale);
	}
	else
	{
		UE_LOG(LogMmdLocale, Verbose, TEXT("Thread-local LC_CTYPE = '%s'"), *AppliedLocale);
	}
}

FMmdScopedUtf8CType::~FMmdScopedUtf8CType()
{
#if PLATFORM_WINDOWS
	if (bActive)
	{
		setlocale(LC_CTYPE, TCHAR_TO_ANSI(*PrevLocale));
	}
	if (PrevThreadConfig != -1)
	{
		_configthreadlocale(PrevThreadConfig);
	}
#else
	if (NewLoc)
	{
		uselocale((locale_t)OldLoc);
		freelocale((locale_t)NewLoc);
	}
#endif
}
