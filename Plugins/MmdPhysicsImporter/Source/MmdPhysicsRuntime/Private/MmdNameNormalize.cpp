// Copyright (c) 2026 masaka1024. MIT License.

#include "MmdNameNormalize.h"

namespace MmdPhysics
{
	namespace NameNormalize
	{
		FString FUeNameMap::ToUe(const FString& Original) const
		{
			if (const FString* Found = OriginalToUe.Find(Original))
			{
				return *Found;
			}
			return ToHalfWidthDigits(Original);
		}

		FUeNameMap BuildUeNameMap(const TArray<FString>& OriginalNames, const TCHAR* Kind)
		{
			FUeNameMap Map;

			// ★FString の比較・ハッシュは大文字小文字を区別しない。FName も同じなので、
			//   UE 上でぶつかる名前をここで同じものとして扱えるのはむしろ都合がよい。
			TSet<FString> Unique;
			for (const FString& O : OriginalNames)
			{
				if (!O.IsEmpty()) Unique.Add(O);
			}

			// 半角化後の綴りでまとめる。振り分けで避けるべき名前の一覧も兼ねる。
			TMap<FString, TArray<FString>> Groups;
			for (const FString& O : Unique)
			{
				Groups.FindOrAdd(ToHalfWidthDigits(O)).Add(O);
			}
			TSet<FString> Taken;
			for (const TPair<FString, TArray<FString>>& G : Groups)
			{
				Taken.Add(G.Key);
			}

			// 振り分けの結果が集合だけで決まるよう、グループも綴り順に処理する。
			TArray<FString> Keys;
			Groups.GetKeys(Keys);
			Keys.Sort([](const FString& A, const FString& B) { return A.Compare(B, ESearchCase::CaseSensitive) < 0; });

			for (const FString& H : Keys)
			{
				TArray<FString>& Members = Groups[H];
				if (Members.Num() == 1)
				{
					Map.OriginalToUe.Add(Members[0], H);
					continue;
				}

				// 元から H と同じ綴り (全角数字なし) が居れば先頭へ。残りはコードポイント順。
				Members.Sort([](const FString& A, const FString& B)
				{
					const bool bAKeeps = !HasFullWidthDigit(A);
					const bool bBKeeps = !HasFullWidthDigit(B);
					if (bAKeeps != bBKeeps) return bAKeeps;
					return A.Compare(B, ESearchCase::CaseSensitive) < 0;
				});

				TArray<FString> Parts;
				Map.OriginalToUe.Add(Members[0], H);
				Parts.Add(FString::Printf(TEXT("%s→%s"), *Members[0], *H));

				int32 Suffix = 2;
				for (int32 m = 1; m < Members.Num(); ++m)
				{
					FString Candidate;
					do
					{
						Candidate = FString::Printf(TEXT("%s_%d"), *H, Suffix++);
					}
					while (Taken.Contains(Candidate));
					Taken.Add(Candidate);

					Map.OriginalToUe.Add(Members[m], Candidate);
					Parts.Add(FString::Printf(TEXT("%s→%s"), *Members[m], *Candidate));
				}

				Map.Collisions.Add(FString::Printf(TEXT("%s: 半角化で '%s' に重なる %d 件を振り分け (%s)"),
					Kind, *H, Members.Num(), *FString::Join(Parts, TEXT(", "))));
			}

			return Map;
		}
	}
}
