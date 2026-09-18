// Copyright (c) 2026 masaka1024. MIT License.

#include "MmdGlbNameNormalize.h"

#include "MmdMiniJson.h"
#include "MmdNameNormalize.h"
#include "MmdPhysicsCoreLog.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

using namespace MmdPhysics;

namespace
{
	constexpr uint32 GlbMagic = 0x46546C67; // "glTF"
	constexpr uint32 ChunkJson = 0x4E4F534A; // "JSON"

	FORCEINLINE uint32 ReadU32LE(const TArray<uint8>& d, int32 Offset)
	{
		return static_cast<uint32>(d[Offset])
			| (static_cast<uint32>(d[Offset + 1]) << 8)
			| (static_cast<uint32>(d[Offset + 2]) << 16)
			| (static_cast<uint32>(d[Offset + 3]) << 24);
	}

	FORCEINLINE void AppendU32LE(TArray<uint8>& Out, uint32 V)
	{
		Out.Add(static_cast<uint8>(V & 0xFF));
		Out.Add(static_cast<uint8>((V >> 8) & 0xFF));
		Out.Add(static_cast<uint8>((V >> 16) & 0xFF));
		Out.Add(static_cast<uint8>((V >> 24) & 0xFF));
	}

	FORCEINLINE int32 HexVal(TCHAR C)
	{
		if (C >= TCHAR('0') && C <= TCHAR('9')) return C - TCHAR('0');
		if (C >= TCHAR('a') && C <= TCHAR('f')) return 10 + (C - TCHAR('a'));
		if (C >= TCHAR('A') && C <= TCHAR('F')) return 10 + (C - TCHAR('A'));
		return -1;
	}

	/**
	 * コードポイントを FString へ足す。
	 * UE 5.8 の TCHAR は全プラットフォームで UTF-16 (PLATFORM_TCHAR_IS_4_BYTES は
	 * 5.8 で非推奨・常に 0)。BMP 外はサロゲートペアに分ける。
	 */
	void AppendCodepoint(FString& Out, uint32 Cp)
	{
		if (Cp <= 0xFFFFu)
		{
			Out.AppendChar(static_cast<TCHAR>(Cp));
			return;
		}
		Cp -= 0x10000u;
		Out.AppendChar(static_cast<TCHAR>(0xD800u + (Cp >> 10)));
		Out.AppendChar(static_cast<TCHAR>(0xDC00u + (Cp & 0x3FFu)));
	}

	/**
	 * JSON 文字列トークンの中身 (引用符を除いた原文) をデコードする。
	 * 名前の**照合**にしか使わない。書き戻しは原文を加工するので、ここを通さない。
	 */
	FString DecodeJsonString(const FString& Raw)
	{
		FString Out;
		Out.Reserve(Raw.Len());

		const int32 N = Raw.Len();
		int32 i = 0;
		while (i < N)
		{
			const TCHAR C = Raw[i];
			if (C != TCHAR('\\')) { Out.AppendChar(C); ++i; continue; }
			if (i + 1 >= N) { Out.AppendChar(C); ++i; continue; }

			const TCHAR E = Raw[i + 1];
			switch (E)
			{
			case TCHAR('"'):  Out.AppendChar(TCHAR('"'));  i += 2; break;
			case TCHAR('\\'): Out.AppendChar(TCHAR('\\')); i += 2; break;
			case TCHAR('/'):  Out.AppendChar(TCHAR('/'));  i += 2; break;
			case TCHAR('b'):  Out.AppendChar(TCHAR('\b')); i += 2; break;
			case TCHAR('f'):  Out.AppendChar(TCHAR('\f')); i += 2; break;
			case TCHAR('n'):  Out.AppendChar(TCHAR('\n')); i += 2; break;
			case TCHAR('r'):  Out.AppendChar(TCHAR('\r')); i += 2; break;
			case TCHAR('t'):  Out.AppendChar(TCHAR('\t')); i += 2; break;
			case TCHAR('u'):
			{
				if (i + 5 >= N) { Out.AppendChar(C); ++i; break; }
				uint32 Cp = 0;
				bool bHex = true;
				for (int32 k = 0; k < 4; ++k)
				{
					const int32 V = HexVal(Raw[i + 2 + k]);
					if (V < 0) { bHex = false; break; }
					Cp = (Cp << 4) | static_cast<uint32>(V);
				}
				if (!bHex) { Out.AppendChar(C); ++i; break; }
				i += 6;

				// サロゲートペア。MMD の名前は BMP に収まるので通常は通らないが、
				// 半端なペアを作らないよう続きも見る。
				if (Cp >= 0xD800u && Cp <= 0xDBFFu && i + 5 < N
					&& Raw[i] == TCHAR('\\') && Raw[i + 1] == TCHAR('u'))
				{
					uint32 Lo = 0;
					bool bLoHex = true;
					for (int32 k = 0; k < 4; ++k)
					{
						const int32 V = HexVal(Raw[i + 2 + k]);
						if (V < 0) { bLoHex = false; break; }
						Lo = (Lo << 4) | static_cast<uint32>(V);
					}
					if (bLoHex && Lo >= 0xDC00u && Lo <= 0xDFFFu)
					{
						Cp = 0x10000u + ((Cp - 0xD800u) << 10) + (Lo - 0xDC00u);
						i += 6;
					}
				}
				AppendCodepoint(Out, Cp);
				break;
			}
			default:
				// 規格外のエスケープ。原文のまま残す。
				Out.AppendChar(C);
				Out.AppendChar(E);
				i += 2;
				break;
			}
		}
		return Out;
	}

	/**
	 * 文字列トークンの原文から全角数字だけを半角へ。
	 * 生の文字と `\uFF1X` エスケープの両方を拾う。エスケープの綴りは変えない
	 * (半角数字はエスケープ不要なので、直した分は素の 1 文字になる)。
	 */
	FString HalfWidthenRawToken(const FString& Raw)
	{
		FString Out;
		Out.Reserve(Raw.Len());

		const int32 N = Raw.Len();
		int32 i = 0;
		while (i < N)
		{
			const TCHAR C = Raw[i];

			if (C == TCHAR('\\') && i + 5 < N && Raw[i + 1] == TCHAR('u'))
			{
				uint32 Cp = 0;
				bool bHex = true;
				for (int32 k = 0; k < 4; ++k)
				{
					const int32 V = HexVal(Raw[i + 2 + k]);
					if (V < 0) { bHex = false; break; }
					Cp = (Cp << 4) | static_cast<uint32>(V);
				}
				if (bHex && NameNormalize::IsFullWidthDigitCodepoint(Cp))
				{
					Out.AppendChar(static_cast<TCHAR>(TCHAR('0') + (Cp - 0xFF10u)));
					i += 6;
					continue;
				}
				// 別の文字のエスケープ。6 文字そのまま送る。
				Out.Append(Raw.Mid(i, 6));
				i += 6;
				continue;
			}

			if (C == TCHAR('\\') && i + 1 < N)
			{
				Out.AppendChar(C);
				Out.AppendChar(Raw[i + 1]);
				i += 2;
				continue;
			}

			if (NameNormalize::IsFullWidthDigit(C))
			{
				Out.AppendChar(static_cast<TCHAR>(TCHAR('0') + (C - NameNormalize::FullWidthZero)));
				++i;
				continue;
			}

			Out.AppendChar(C);
			++i;
		}
		return Out;
	}

	/**
	 * 振り分けた名前 (`H_2` など) を JSON 文字列トークンの中身として書く。
	 * 非 ASCII はそのまま (チャンクは UTF-8)、引用符・バックスラッシュ・制御文字だけ逃がす。
	 */
	FString EscapeJsonNameToken(const FString& In)
	{
		FString Out;
		Out.Reserve(In.Len());
		for (const TCHAR C : In)
		{
			if (C == TCHAR('"')) { Out += TEXT("\\\""); continue; }
			if (C == TCHAR('\\')) { Out += TEXT("\\\\"); continue; }
			if (C < 0x20) { Out += FString::Printf(TEXT("\\u%04x"), static_cast<uint32>(C)); continue; }
			Out.AppendChar(C);
		}
		return Out;
	}

	/**
	 * JSON の原文を走査し、NameMap に載っている文字列トークンだけを UE 名へ書き戻す。
	 *
	 * ★構造 (数値・キー・区切り) には一切触らない。文字列トークンの中だけを見る。
	 *   書き換えないトークンは**原文をそのまま複写**する (再エンコードしない)。
	 *
	 * ★同じ綴りの文字列が名前以外の場所にもあれば、そこも一緒に直る。
	 *   ボーン名と同綴りのマテリアル名などが該当するが、直っても困らないし、
	 *   むしろ extras.mmd 側にボーン名が文字列で載っている場合は揃ってくれた方がよい。
	 */
	bool RewriteJsonNames(const FString& InJson, const TMap<FString, FString>& NameMap, FString& OutJson)
	{
		OutJson.Reset();
		OutJson.Reserve(InJson.Len());

		const int32 N = InJson.Len();
		int32 i = 0;
		while (i < N)
		{
			const TCHAR C = InJson[i];
			if (C != TCHAR('"'))
			{
				OutJson.AppendChar(C);
				++i;
				continue;
			}

			// 開き " から、エスケープされていない " まで。
			int32 j = i + 1;
			while (j < N)
			{
				if (InJson[j] == TCHAR('\\')) { j += 2; continue; }
				if (InJson[j] == TCHAR('"')) break;
				++j;
			}
			if (j >= N)
			{
				// 閉じない文字列。壊れた JSON なので、残りをそのまま出して失敗を返す。
				OutJson.Append(InJson.Mid(i));
				return false;
			}

			const FString RawToken = InJson.Mid(i + 1, j - i - 1);
			const FString* NewName = NameMap.Find(DecodeJsonString(RawToken));

			OutJson.AppendChar(TCHAR('"'));
			if (NewName == nullptr)
			{
				OutJson.Append(RawToken);
			}
			else if (NewName->Equals(NameNormalize::ToHalfWidthDigits(DecodeJsonString(RawToken)), ESearchCase::CaseSensitive))
			{
				// 半角化だけで済む名前は原文の綴り (エスケープ形式など) を保って数字だけ直す。
				OutJson.Append(HalfWidthenRawToken(RawToken));
			}
			else
			{
				// 振り分けた名前 (H_2 など) は原文から作れないので書き直す。
				OutJson.Append(EscapeJsonNameToken(*NewName));
			}
			OutJson.AppendChar(TCHAR('"'));

			i = j + 1;
		}
		return true;
	}

	/** skins[].joints が指すノードの名前を、joints の並び順で集める。 */
	void CollectJointNames(const TSharedPtr<MmdJsonValue>& Root, TArray<FString>& Out)
	{
		const FMmdJsonObject* RootObj = MiniJson::Obj(Root);
		const FMmdJsonArray* Nodes = MiniJson::Arr(MiniJson::Get(RootObj, TEXT("nodes")));
		const FMmdJsonArray* Skins = MiniJson::Arr(MiniJson::Get(RootObj, TEXT("skins")));
		if (Nodes == nullptr || Skins == nullptr) return;

		TSet<int32> Seen;
		for (const TSharedPtr<MmdJsonValue>& Skin : *Skins)
		{
			const FMmdJsonArray* Joints = MiniJson::Arr(MiniJson::Get(MiniJson::Obj(Skin), TEXT("joints")));
			if (Joints == nullptr) continue;

			for (const TSharedPtr<MmdJsonValue>& J : *Joints)
			{
				const int32 Ni = MiniJson::Int(J);
				if (Ni < 0 || Ni >= Nodes->Num()) continue;
				if (Seen.Contains(Ni)) continue;
				Seen.Add(Ni);

				const FString Name = MiniJson::Str(MiniJson::Get(MiniJson::Obj((*Nodes)[Ni]), TEXT("name")));
				if (!Name.IsEmpty()) Out.Add(Name);
			}
		}
	}

	/** meshes[].extras.targetNames と meshes[].primitives[].extras.targetNames を集める (メッシュ単位)。 */
	void CollectMorphNames(const TSharedPtr<MmdJsonValue>& Root, TArray<TArray<FString>>& OutPerMesh)
	{
		const FMmdJsonArray* Meshes = MiniJson::Arr(MiniJson::Get(MiniJson::Obj(Root), TEXT("meshes")));
		if (Meshes == nullptr) return;

		for (const TSharedPtr<MmdJsonValue>& M : *Meshes)
		{
			const FMmdJsonObject* MeshObj = MiniJson::Obj(M);
			TArray<FString> Names;

			auto AppendTargetNames = [&Names](const FMmdJsonObject* Owner)
			{
				const FMmdJsonArray* T = MiniJson::Arr(
					MiniJson::Get(MiniJson::Obj(MiniJson::Get(Owner, TEXT("extras"))), TEXT("targetNames")));
				if (T == nullptr) return;
				for (const TSharedPtr<MmdJsonValue>& V : *T)
				{
					const FString S = MiniJson::Str(V);
					if (!S.IsEmpty()) Names.Add(S);
				}
			};

			AppendTargetNames(MeshObj);

			// エクスポーターによっては primitives 側に置く。両方見る。
			if (const FMmdJsonArray* Prims = MiniJson::Arr(MiniJson::Get(MeshObj, TEXT("primitives"))))
			{
				for (const TSharedPtr<MmdJsonValue>& P : *Prims)
				{
					AppendTargetNames(MiniJson::Obj(P));
				}
			}

			if (Names.Num() > 0) OutPerMesh.Add(MoveTemp(Names));
		}
	}
}

FMmdGlbNormalizeResult FMmdGlbNameNormalize::NormalizeBytes(const TArray<uint8>& InGlb, TArray<uint8>& OutGlb)
{
	FMmdGlbNormalizeResult Result;
	OutGlb.Reset();

	if (InGlb.Num() < 12)
	{
		Result.Message = TEXT("GLB が短すぎる (ヘッダに満たない)。");
		return Result;
	}
	if (ReadU32LE(InGlb, 0) != GlbMagic)
	{
		Result.Message = TEXT("GLB マジックが不正 (glTF ではない)。");
		return Result;
	}

	// --- チャンクを列挙する (中身は解釈しない) ---
	struct FChunk { uint32 Type; int32 DataOffset; int32 DataLen; };
	TArray<FChunk> Chunks;
	int32 JsonChunk = INDEX_NONE;
	{
		int32 Off = 12;
		while (Off + 8 <= InGlb.Num())
		{
			const int32 Clen = static_cast<int32>(ReadU32LE(InGlb, Off));
			const uint32 Ctype = ReadU32LE(InGlb, Off + 4);
			const int32 Cdata = Off + 8;
			if (Clen < 0 || Cdata + Clen > InGlb.Num()) break; // 壊れたチャンク長で範囲外を読まない

			if (Ctype == ChunkJson && JsonChunk == INDEX_NONE) JsonChunk = Chunks.Num();
			Chunks.Add({ Ctype, Cdata, Clen });

			Off = Cdata + Clen;
			if ((Clen & 3) != 0) Off += 4 - (Clen & 3); // 4バイト境界パディング
		}
	}
	if (JsonChunk == INDEX_NONE)
	{
		Result.Message = TEXT("GLB に JSON チャンクが無い。");
		return Result;
	}

	const FChunk& Jc = Chunks[JsonChunk];
	const FString Json(FUTF8ToTCHAR(reinterpret_cast<const ANSICHAR*>(InGlb.GetData() + Jc.DataOffset), Jc.DataLen));

	// --- 直す名前を集める ---
	const TSharedPtr<MmdJsonValue> Root = MiniJson::Parse(Json);
	if (!Root.IsValid())
	{
		Result.Message = TEXT("JSON チャンクを解釈できない。");
		return Result;
	}

	TArray<FString> JointNames;
	CollectJointNames(Root, JointNames);

	TArray<TArray<FString>> MorphNamesPerMesh;
	CollectMorphNames(Root, MorphNamesPerMesh);

	if (JointNames.Num() == 0)
	{
		Result.Warnings.Add(TEXT("skins/joints が無いためボーン名は見ていない。"));
	}

	// --- UE 名を決める。半角化で衝突するものは一意な名前へ振り分ける (中止しない) ---
	// ★振り分けの規則は NameNormalize::BuildUeNameMap。照合側 (物理・モーフ・移動フラグ) も
	//   同じ関数で同じ対応を作り直すので、ここで独自に名前を決めてはいけない。
	auto AddToMap = [&Result](const NameNormalize::FUeNameMap& Map, int32& Counter)
	{
		for (const FString& C : Map.Collisions)
		{
			Result.Collisions.Add(C);
			UE_LOG(LogMmdPhysics, Warning, TEXT("[MmdPhysics] %s"), *C);
		}
		for (const TPair<FString, FString>& P : Map.OriginalToUe)
		{
			if (P.Key.Equals(P.Value, ESearchCase::CaseSensitive)) continue;
			if (const FString* Existing = Result.NameMap.Find(P.Key))
			{
				// ボーンとモーフに同じ綴りがあり、振り分け先が食い違う場合。
				// JSON の書き換えは綴り単位なので片方しか採れない。先に決めた方を残す。
				if (!Existing->Equals(P.Value, ESearchCase::CaseSensitive))
				{
					Result.Warnings.Add(FString::Printf(
						TEXT("'%s' はボーンとモーフで振り分け先が違う ('%s' / '%s')。'%s' を採った。"),
						*P.Key, **Existing, *P.Value, **Existing));
				}
				++Counter;
				continue;
			}
			Result.NameMap.Add(P.Key, P.Value);
			++Counter;
		}
	};
	AddToMap(NameNormalize::BuildUeNameMap(JointNames, TEXT("ボーン")), Result.BonesRenamed);
	for (const TArray<FString>& Names : MorphNamesPerMesh)
	{
		AddToMap(NameNormalize::BuildUeNameMap(Names, TEXT("モーフ")), Result.MorphsRenamed);
	}
	for (const FString& W : Result.Warnings)
	{
		UE_LOG(LogMmdPhysics, Warning, TEXT("[MmdPhysics] %s"), *W);
	}

	if (Result.NameMap.Num() == 0)
	{
		// 直すところが無い。原本をそのまま使ってよい。
		OutGlb = InGlb;
		Result.bSuccess = true;
		Result.Message = TEXT("全角数字は無かった。原本をそのまま取り込める。");
		return Result;
	}

	// --- JSON を書き換える ---
	FString NewJson;
	if (!RewriteJsonNames(Json, Result.NameMap, NewJson))
	{
		Result.Message = TEXT("JSON の文字列が閉じていない (壊れた GLB)。");
		return Result;
	}

	// --- GLB を組み直す。JSON チャンク以外は原文を複写する ---
	const FTCHARToUTF8 NewJsonUtf8(*NewJson);
	const int32 NewJsonLen = NewJsonUtf8.Length();

	int32 Total = 12;
	for (int32 c = 0; c < Chunks.Num(); ++c)
	{
		const int32 Len = (c == JsonChunk) ? NewJsonLen : Chunks[c].DataLen;
		Total += 8 + Len + ((4 - (Len & 3)) & 3);
	}

	OutGlb.Reserve(Total);
	AppendU32LE(OutGlb, GlbMagic);
	AppendU32LE(OutGlb, ReadU32LE(InGlb, 4)); // version は原本のまま
	AppendU32LE(OutGlb, static_cast<uint32>(Total));

	for (int32 c = 0; c < Chunks.Num(); ++c)
	{
		const bool bIsJson = (c == JsonChunk);
		const int32 Len = bIsJson ? NewJsonLen : Chunks[c].DataLen;

		AppendU32LE(OutGlb, static_cast<uint32>(Len));
		AppendU32LE(OutGlb, Chunks[c].Type);

		if (bIsJson)
		{
			OutGlb.Append(reinterpret_cast<const uint8*>(NewJsonUtf8.Get()), NewJsonLen);
		}
		else
		{
			OutGlb.Append(InGlb.GetData() + Chunks[c].DataOffset, Len);
		}

		// glTF 2.0 のチャンクは 4 バイト境界に揃える。JSON の詰め物は空白、それ以外は 0。
		const uint8 Pad = bIsJson ? 0x20 : 0x00;
		for (int32 p = ((4 - (Len & 3)) & 3); p > 0; --p) OutGlb.Add(Pad);
	}

	Result.bSuccess = true;
	Result.Message = FString::Printf(
		TEXT("全角数字を半角へ直した: ボーン %d 件 / モーフ %d 件 (綴り %d 種 / 衝突を振り分け %d 件)。"),
		Result.BonesRenamed, Result.MorphsRenamed, Result.NameMap.Num(), Result.Collisions.Num());
	UE_LOG(LogMmdPhysics, Log, TEXT("[MmdPhysics] %s"), *Result.Message);
	return Result;
}

FMmdGlbNormalizeResult FMmdGlbNameNormalize::NormalizeFile(const FString& InPath, const FString& OutPath)
{
	FMmdGlbNormalizeResult Result;

	TArray<uint8> InBytes;
	if (!FFileHelper::LoadFileToArray(InBytes, *InPath))
	{
		Result.Message = FString::Printf(TEXT("GLB を読めない: %s"), *InPath);
		return Result;
	}

	TArray<uint8> OutBytes;
	Result = NormalizeBytes(InBytes, OutBytes);
	if (!Result.bSuccess) return Result;

	IFileManager::Get().MakeDirectory(*FPaths::GetPath(OutPath), /*Tree=*/true);

	if (!FFileHelper::SaveArrayToFile(OutBytes, *OutPath))
	{
		Result.bSuccess = false;
		Result.Message = FString::Printf(TEXT("半角化した GLB を書けない: %s"), *OutPath);
	}
	return Result;
}

FString FMmdGlbNameNormalize::MakeNormalizedPath(const FString& SourceGlbPath)
{
	// ★ファイル名は原本と同じにする (フォルダで分ける)。Interchange はアセット名と
	//   シーン名のサブフォルダを**ファイル名から**付けるので、`_ue` などを足すと
	//   `/Game/IA/IA_ue/SkeletalMeshes/IA_ue` のように原本と別の名前・別の場所になり、
	//   既存アセットの取り込み直しにならない。
	const FString BaseName = FPaths::GetBaseFilename(SourceGlbPath);
	return FPaths::ConvertRelativePathToFull(
		FPaths::ProjectSavedDir() / TEXT("MmdPhysicsImporter") / BaseName
		/ (BaseName + TEXT(".glb")));
}
