// MmdScopedUtf8CType.h
// アニメ取り込み中だけ「現在のスレッド」の LC_CTYPE を UTF-8 系に切り替える RAII。
// プロセス全体のロケール(setlocale のグローバル設定)は変更しない。
//
//  Mac / Linux : newlocale + uselocale(スレッド局所)
//  Windows     : _configthreadlocale(_ENABLE_PER_THREAD_LOCALE) + setlocale(スレッド局所)
//
// 背景: Control Rig の SanitizeName が iswalpha を使っており、"C" ロケールでは
//       日本語が文字と判定されず '_' に潰れるため。
#pragma once

#include "CoreMinimal.h"

// MmdPhysicsCore モジュールに置く (API マクロはモジュール名に合わせてある)
class MMDPHYSICSCORE_API FMmdScopedUtf8CType
{
public:
	FMmdScopedUtf8CType();
	~FMmdScopedUtf8CType();

	FMmdScopedUtf8CType(const FMmdScopedUtf8CType&) = delete;
	FMmdScopedUtf8CType& operator=(const FMmdScopedUtf8CType&) = delete;

	/** 切り替えに成功したか */
	bool IsActive() const { return bActive; }

	/** 実際に適用したロケール名(ログ・テスト用) */
	const FString& GetAppliedLocale() const { return AppliedLocale; }

	/** このスレッドで日本語(あ・ア・漢)が iswalpha で「文字」と判定されるか */
	static bool IsJapaneseAlphaOnThisThread();

private:
	bool    bActive = false;
	FString AppliedLocale;

#if PLATFORM_WINDOWS
	int     PrevThreadConfig = -1;  // _configthreadlocale の元の設定
	FString PrevLocale;             // 切替前のスレッドの LC_CTYPE 名
#else
	void*   NewLoc = nullptr;       // locale_t
	void*   OldLoc = nullptr;       // locale_t(LC_GLOBAL_LOCALE の場合あり)
#endif
};
