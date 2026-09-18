// Copyright (c) 2026 masaka1024. MIT License.
// ===========================================================================
// 全角数字 (U+FF10-FF19) を半角へ直すだけの小さなヘルパ。
//
// ★なぜ「全角数字だけ」なのか。
//   URigHierarchy::SanitizeName (RigHierarchy.cpp) の合否は
//
//     FChar::IsAlpha(C) || C=='_' || C=='-' || C=='.' || C=='|' || FChar::IsDigit(C) || (i>0 && C==' ')
//
//   で、通らない文字は '_' に潰れる。仮名・漢字は LC_CTYPE を UTF-8 ロケールへ
//   上げれば iswalpha が拾うので通る (Mac のエディタは起動時に上げる。
//   MmdPhysicsEditorModule.cpp の【日本語名と LC_CTYPE】)。
//   しかし全角数字は iswalpha=0 かつ iswdigit=0 で、**どちらの検査にも入らない**。
//   iswdigit が ASCII の 0-9 しか返さないのは POSIX の規定なのでロケールでは変わらず、
//   判定式はエンジン側にあって差し替えられない。
//
//     全角２  iswalpha=0  iswdigit=0  iswalnum=1   ← 潰れる
//     半角2   iswalpha=0  iswdigit=1               ← 通る
//
//   結果 PMX 標準の指ボーン `右人指１` `右人指２` `右人指３` が揃って `右人指_` に
//   潰れて衝突し、衝突した分のトラックが取り込みで捨てられる (IA で 54 本中 18 本が脱落)。
//
//   つまり**半角化が要るのは数字だけ**。全角英字も仮名も漢字も IsAlpha を通るので
//   触ってはいけない。触ると原本の名前を無用に変えることになる。
//
// ★Runtime に置いてある理由。
//   .glb の書き換えそのものは取り込み時にしか走らない (MmdGlbNameNormalize, Editor) が、
//   **照合側は実行時にも要る**。AnimNode_MmdPhysics は extras.mmd 由来のボーン名で
//   スケルトンを引くので、参照している .glb が原本 (全角) でも取り込み済みスケルトン
//   (半角) に当たるよう、引き当ての最後にこの正規化を挟む。
//   (FindBoneIndexForPmxName を参照)
// ===========================================================================

#pragma once

#include "CoreMinimal.h"

namespace MmdPhysics
{
	namespace NameNormalize
	{
		/** '０' (U+FF10) */
		inline constexpr TCHAR FullWidthZero = static_cast<TCHAR>(0xFF10);
		/** '９' (U+FF19) */
		inline constexpr TCHAR FullWidthNine = static_cast<TCHAR>(0xFF19);

		FORCEINLINE bool IsFullWidthDigit(TCHAR C)
		{
			return C >= FullWidthZero && C <= FullWidthNine;
		}

		FORCEINLINE bool IsFullWidthDigitCodepoint(uint32 Cp)
		{
			return Cp >= 0xFF10u && Cp <= 0xFF19u;
		}

		FORCEINLINE bool HasFullWidthDigit(const FString& In)
		{
			for (const TCHAR C : In)
			{
				if (IsFullWidthDigit(C)) return true;
			}
			return false;
		}

		/** 全角数字だけを半角へ。それ以外の文字は 1 文字も変えない。 */
		FORCEINLINE FString ToHalfWidthDigits(const FString& In)
		{
			if (!HasFullWidthDigit(In)) return In;   // 変えないものは同じ実体を返す

			FString Out = In;
			for (TCHAR& C : Out)
			{
				if (IsFullWidthDigit(C))
				{
					C = static_cast<TCHAR>(TCHAR('0') + (C - FullWidthZero));
				}
			}
			return Out;
		}

		/**
		 * 原本名 → UE 上の名前 の対応。
		 *
		 * ★UE 上のボーン・トラック・カーブ名は**半角化した名前**にそろえる (Mac/Windows 共通)。
		 *   原本名 (全角) は .glb 側に残っており、照合は全角・半角のどちらからでも引ける。
		 *
		 * ★半角化で名前がぶつかる場合 (`右腕1` と `右腕１` が並存する等) は一意な名前へ振り分ける。
		 *   振り分けは**名前の集合だけで決まる**純関数にしてある。取り込み (Editor) と
		 *   照合 (Runtime の物理・モーフ) が同じ集合からそれぞれ同じ対応を作り直せるように。
		 *   並び順には依存しない。
		 */
		struct MMDPHYSICSRUNTIME_API FUeNameMap
		{
			/** 原本名 → UE 名。名前が変わらないものも含む (集合の全員が載る)。 */
			TMap<FString, FString> OriginalToUe;

			/** 振り分けが起きたグループの説明 (人が読む用)。空なら衝突なし。 */
			TArray<FString> Collisions;

			/** UE 名を返す。集合に無い名前は半角化だけして返す。 */
			FString ToUe(const FString& Original) const;
		};

		/**
		 * 名前の集合から UE 名を決める。
		 *
		 * 規則 (グループ = 半角化すると同じ綴り H になる原本名の集まり):
		 *   - 1 人だけのグループは H。
		 *   - 2 人以上なら、元から H と同じ綴りの名前 (全角数字を含まない) がそのまま H を取る。
		 *     居なければコードポイント順で先頭が H を取る。
		 *     残りはコードポイント順に `H_2` `H_3` … を取る。集合の中の他の名前 (半角化後) と
		 *     振り分け済みの名前は避ける。
		 *
		 * ★`_` と ASCII 数字は Control Rig の SanitizeName をどのロケールでも通るので、
		 *   振り分けた名前がさらに潰れることはない。
		 */
		MMDPHYSICSRUNTIME_API FUeNameMap BuildUeNameMap(const TArray<FString>& OriginalNames, const TCHAR* Kind);
	}
}
