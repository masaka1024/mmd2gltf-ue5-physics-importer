// MmdScopedUtf8CTypeTest.cpp
// テストフラグは既存テスト (MmdPhysics.Editor.GlbNameNormalize 等) と同じ EditorContext | EngineFilter
#include "MmdScopedUtf8CType.h"
#include "Misc/AutomationTest.h"

#include <locale.h>

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMmdScopedUtf8CTypeTest,
	"MmdPhysics.Locale.ScopedUtf8CType",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMmdScopedUtf8CTypeTest::RunTest(const FString& Parameters)
{
	const char* BeforeRaw = setlocale(LC_CTYPE, nullptr);
	const FString Before = BeforeRaw ? FString(ANSI_TO_TCHAR(BeforeRaw)) : FString();

	// 参考情報: スコープ外で日本語が文字扱いか(Windowsでは元々trueの可能性あり)
	AddInfo(FString::Printf(TEXT("Outside scope: Japanese alpha = %s (global LC_CTYPE='%s')"),
		FMmdScopedUtf8CType::IsJapaneseAlphaOnThisThread() ? TEXT("true") : TEXT("false"), *Before));

	{
		FMmdScopedUtf8CType Scope;
		TestTrue(TEXT("Scope is active"), Scope.IsActive());
		TestTrue(TEXT("Japanese is alpha inside scope"), FMmdScopedUtf8CType::IsJapaneseAlphaOnThisThread());
		AddInfo(FString::Printf(TEXT("Applied locale: %s"), *Scope.GetAppliedLocale()));
	}

	const char* AfterRaw = setlocale(LC_CTYPE, nullptr);
	const FString After = AfterRaw ? FString(ANSI_TO_TCHAR(AfterRaw)) : FString();
	TestEqual(TEXT("Global LC_CTYPE is restored"), After, Before);

	return true;
}

#endif
