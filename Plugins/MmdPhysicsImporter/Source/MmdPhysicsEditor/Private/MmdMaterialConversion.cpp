// Copyright (c) 2026 masaka1024. MIT License.

#include "MmdMaterialConversion.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "Factories/MaterialFactoryNew.h"
#include "Factories/MaterialInstanceConstantFactoryNew.h"
#include "Factories/TextureFactory.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionAppendVector.h"
#include "Materials/MaterialExpressionCeil.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionSubtract.h"
#include "Materials/MaterialExpressionDotProduct.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionOneMinus.h"
#include "Materials/MaterialExpressionSaturate.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionTransform.h"
#include "Materials/MaterialExpressionTwoSidedSign.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionVertexNormalWS.h"
#include "Materials/MaterialInstanceConstant.h"
#include "IImageWrapperModule.h"
#include "MmdGlbMaterialReader.h"
#include "MmdPhysicsCoreLog.h"
#include "ObjectTools.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "UObject/SavePackage.h"

#define LOCTEXT_NAMESPACE "MmdMaterialConversion"

using namespace MmdPhysics;

namespace
{
	const TCHAR* KMasterName = TEXT("M_MmdToon");
	const TCHAR* KMasterTranslucentName = TEXT("M_MmdToonTranslucent");
	/** 輪郭線 (反転ハル) 用のマスター。本体とは別のコンポーネントで使う。 */
	const TCHAR* KOutlineName = TEXT("M_MmdOutline");

	/**
	 * GLB から取り出したテクスチャの置き場 (モデルのフォルダ配下)。
	 * 移植元 Unity 版の "MMD_ExtractedTextures" と同じ名前にしてある。
	 */
	const TCHAR* KExtractedFolderName = TEXT("MMD_ExtractedTextures");

	const TCHAR* MasterAssetName(EMmdMasterVariant Variant)
	{
		return Variant == EMmdMasterVariant::Translucent ? KMasterTranslucentName : KMasterName;
	}

	/** glTF の alphaMode が BLEND か (= 真の半透明として扱うか)。 */
	bool IsBlendMaterial(const MmdMaterialInfo& Info)
	{
		return Info.AlphaMode.Equals(TEXT("BLEND"), ESearchCase::IgnoreCase);
	}

	bool SavePackageOfAsset(UObject* Asset)
	{
		if (Asset == nullptr) return false;
		UPackage* Package = Asset->GetOutermost();
		const FString FileName = FPackageName::LongPackageNameToFilename(
			Package->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		Args.SaveFlags = SAVE_NoError;
		Args.Error = GWarn;
		return UPackage::SavePackage(Package, nullptr, *FileName, Args);
	}

	template <typename T>
	T* MakeNode(UMaterial* Mat, int32 X, int32 Y)
	{
		return Cast<T>(UMaterialEditingLibrary::CreateMaterialExpression(Mat, T::StaticClass(), X, Y));
	}

	/**
	 * ノード同士を繋ぐ。★戻り値を必ず見ること。
	 * ConnectMaterialExpressions は入力名が違っても false を返すだけで例外にならないので、
	 * 黙って未接続のグラフが出来上がり「灰色/黒のまま」という形でしか気付けない。
	 */
	bool Connect(UMaterialExpression* From, const TCHAR* FromOut, UMaterialExpression* To, const TCHAR* ToIn)
	{
		const bool bOk = UMaterialEditingLibrary::ConnectMaterialExpressions(From, FromOut, To, ToIn);
		if (!bOk)
		{
			UE_LOG(LogMmdPhysics, Error, TEXT("[MmdPhysics] マテリアルのノード接続に失敗: %s.%s -> %s.%s"),
				From ? *From->GetClass()->GetName() : TEXT("null"), FromOut,
				To ? *To->GetClass()->GetName() : TEXT("null"), ToIn);
		}
		return bOk;
	}

	/**
	 * glTF の画像名 ("眼球４.bmp") から、Interchange が取り込んだテクスチャアセットを探す。
	 * Interchange は拡張子のドットを '_' に置き換えるので、その形も試す。
	 */
	/** アセット名でプロジェクト全体を探す (共有トゥーンのように別フォルダに置かれるもの用)。 */
	UTexture2D* FindTextureAnywhere(const FString& AssetName)
	{
		if (AssetName.IsEmpty()) return nullptr;
		IAssetRegistry& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
		TArray<FAssetData> Assets;
		AR.GetAssetsByClass(UTexture2D::StaticClass()->GetClassPathName(), Assets, /*bSearchSubClasses=*/true);
		for (const FAssetData& A : Assets)
		{
			if (A.AssetName.ToString().Equals(AssetName, ESearchCase::IgnoreCase))
			{
				return Cast<UTexture2D>(A.GetAsset());
			}
		}
		return nullptr;
	}

	/** glTF の画像名 ("眼球４.bmp") を UE のアセット名 ("眼球４_bmp") にする。 */
	FString TextureAssetNameFor(const FString& ImageName)
	{
		// アセット名にドットは使えない (Interchange も同じ置き換えをする)。
		return ObjectTools::SanitizeObjectName(ImageName.Replace(TEXT("."), TEXT("_")));
	}

	UTexture2D* FindImportedTexture(const FString& ImageName, const FString& PackagePath)
	{
		if (ImageName.IsEmpty()) return nullptr;

		// アセット名にドットは使えないので、候補は必ずサニタイズしたものにする。
		// (Interchange は "眼球４.bmp" を "眼球４_bmp" として取り込む)
		TArray<FString> Candidates;
		Candidates.AddUnique(TextureAssetNameFor(ImageName));                              // 眼球４_bmp
		Candidates.AddUnique(ObjectTools::SanitizeObjectName(FPaths::GetBaseFilename(ImageName))); // 眼球４

		// 1) まず同じフォルダ (モデルと一緒に取り込まれたテクスチャ)、
		//    次に GLB から取り出した置き場 (前回の変換で作ったもの)。
		const FString SearchPaths[] = { PackagePath, PackagePath / KExtractedFolderName };
		for (const FString& Dir : SearchPaths)
		{
			for (const FString& Name : Candidates)
			{
				const FString ObjPath = Dir / Name + TEXT(".") + Name;
				if (UTexture2D* Tex = LoadObject<UTexture2D>(nullptr, *ObjPath, nullptr, LOAD_NoWarn | LOAD_Quiet))
				{
					return Tex;
				}
			}
		}

		// 2) 見つからなければプロジェクト全体を名前で検索する。
		//    共有トゥーン (toon01..toon10) はモデルに同梱されず、ユーザーが好きな場所へ
		//    取り込むため。移植元 Unity 版の FindSharedToonTexture と同じ挙動。
		for (const FString& Name : Candidates)
		{
			if (UTexture2D* Tex = FindTextureAnywhere(Name)) return Tex;
		}
		return nullptr;
	}

	/**
	 * glTF texture index → UTexture2D。名前で見つからなければ GLB から取り出す。
	 * 移植元 Unity 版の FindOrExtractTextureByIndex + extractedTextureCache に相当する。
	 *
	 * ★toon / sphere / origTexture の画像は glTF の標準マテリアルから参照されていないため、
	 *   Interchange ではアセット化されない。名前で探すだけでは取りこぼす。
	 */
	class FTextureResolver
	{
	public:
		FTextureResolver(const MmdMaterialSet& InSet, const FString& InPackagePath, const FString& InGlbPath)
			: Set(InSet), PackagePath(InPackagePath), GlbPath(InGlbPath)
		{
		}

		UTexture2D* FindOrExtract(int32 TextureIndex)
		{
			if (TextureIndex < 0) return nullptr;
			// 失敗 (nullptr) もキャッシュする。同じ材質が何度も同じ抽出を試して
			// 同じ警告を並べるのを避けるため (移植元も結果をそのまま入れている)。
			if (UTexture2D** Cached = Cache.Find(TextureIndex)) return *Cached;

			UTexture2D* Tex = nullptr;
			if (Set.HasTexture(TextureIndex))
			{
				Tex = FindImportedTexture(Set.TextureImageNames[TextureIndex], PackagePath);
			}
			if (Tex == nullptr)
			{
				Tex = Extract(TextureIndex);
			}
			Cache.Add(TextureIndex, Tex);
			return Tex;
		}

		int32 NumExtracted() const { return ExtractedCount; }

	private:
		UTexture2D* Extract(int32 TextureIndex)
		{
			// GLB の読み込みは全部インポート済みなら不要なので、最初に要ったときだけ行う。
			if (!bPrepared)
			{
				bPrepared = true;
				TArray<FString> Warnings;
				Extractor.Prepare(GlbPath, Warnings);
				for (const FString& W : Warnings)
				{
					UE_LOG(LogMmdPhysics, Warning, TEXT("[MmdPhysics] %s"), *W);
				}
			}
			if (!Extractor.IsReady()) return nullptr;

			MmdGlbImage Image;
			TArray<FString> Warnings;
			if (!Extractor.ExtractTexture(TextureIndex, Image, Warnings))
			{
				for (const FString& W : Warnings)
				{
					UE_LOG(LogMmdPhysics, Warning, TEXT("[MmdPhysics] %s"), *W);
				}
				return nullptr;
			}

			const FString AssetName = Image.Name.IsEmpty()
				? FString::Printf(TEXT("tex_%d"), TextureIndex)
				: TextureAssetNameFor(Image.Name);
			const FString FolderPath = PackagePath / KExtractedFolderName;
			const FString PackageName = FolderPath / AssetName;

			UPackage* Package = CreatePackage(*PackageName);
			if (Package == nullptr)
			{
				UE_LOG(LogMmdPhysics, Warning,
					TEXT("[MmdPhysics] テクスチャの保存先パッケージを作れません: %s"), *PackageName);
				return nullptr;
			}
			Package->FullyLoad();

			// ★UTextureFactory に PNG/JPEG のファイルそのものを渡してデコードさせる。
			//   自前で ImageWrapper からピクセルを起こすより、圧縮設定・ガンマ・ミップの
			//   既定値が通常のインポートと揃うので、見た目が食い違わない。
			UTextureFactory::SuppressImportOverwriteDialog();
			UTextureFactory* Factory = NewObject<UTextureFactory>();
			Factory->AddToRoot();
			const uint8* Ptr = Image.Bytes.GetData();
			UTexture2D* Tex = Cast<UTexture2D>(Factory->FactoryCreateBinary(
				UTexture2D::StaticClass(), Package, *AssetName, RF_Public | RF_Standalone,
				nullptr, *Image.Extension(), Ptr, Ptr + Image.Bytes.Num(), GWarn));
			Factory->RemoveFromRoot();

			if (Tex == nullptr)
			{
				UE_LOG(LogMmdPhysics, Warning,
					TEXT("[MmdPhysics] テクスチャ番号 %d ('%s') のデコードに失敗しました。"),
					TextureIndex, *Image.Name);
				return nullptr;
			}

			FAssetRegistryModule::AssetCreated(Tex);
			Tex->PostEditChange();
			Tex->MarkPackageDirty();
			SavePackageOfAsset(Tex);
			ExtractedCount++;

			UE_LOG(LogMmdPhysics, Log,
				TEXT("[MmdPhysics] テクスチャ番号 %d を GLB から取り出しました: %s"),
				TextureIndex, *PackageName);
			return Tex;
		}

		const MmdMaterialSet& Set;
		FString PackagePath;
		FString GlbPath;

		GlbImageExtractor Extractor;
		bool bPrepared = false;
		int32 ExtractedCount = 0;
		TMap<int32, UTexture2D*> Cache;
	};
}

// ===========================================================================
// 移植元の renderQueue 2 帯構成を、UE の描画パスへ対応させる。
//
// 移植元 (MmdPhysicsImporterWindow.cs 405〜457 行付近) はこうしていた:
//   ・origTexture を持つ材質は MASK でも Transparent(modeInt=2) へ昇格させ、
//     プリベイク前の無加工テクスチャで実アルファブレンドする
//   ・ただし全部を透明キューへ入れると、透明キュー内はサブメッシュ番号順 + TwoPass の
//     深度書き込みで描かれるため「前髪(若い番号)の向こうのメガネ(大きい番号)が消える」
//     (Tda式ミクV4X で実測)。そこで alphaClass で 2 帯に分けた:
//       alphaClass=="blend" (真の半透明: 透け髪・髪影・チーク・柔らかい眉) → 3000 + 材質番号
//       それ以外 (mask 由来の昇格組: 肌・服・メガネ。見た目はほぼ不透明)   → 2452 + 材質番号
//
// UE には renderQueue が無い。代わりに「どのパスで描かれるか」が帯そのものなので、
// 帯の振り分けをマスターマテリアルの選択に置き換える:
//   ・真の半透明   → Translucent マスター。半透明パスで、しきい値で切らずに混ぜる
//   ・mask 昇格組  → Masked マスターのまま。不透明パスで描かれ深度を書くので、
//                    構造的に必ず真の半透明より先に描かれる (= AlphaTest 帯と同じ役割)
//   ・材質順       → UE の半透明ソートキーは Priority(16) → Distance(32) →
//                    MeshIdInPrimitive(16) で、同一スケルタルメッシュのセクションは
//                    前 2 つが同値になるため最下位のセクション順 (= スロット順) で決まる。
//                    移植元の "+ 材質番号" は自動的に満たされるので何もしない
//
// ★mask 昇格組は Masked のままなので、縁のソフトさ (移植元の TwoPass 相当) は出ない。
//   代わりにプリベイク版テクスチャを使う。プリベイク版は縁の画素が下地と合成済みで、
//   アルファテストで描くことを前提に作られているため、これが正しい組み合わせになる
//   (詳しくは PlanMaterial の bUseOrigTexture の注記)。
//   DitherTemporalAA (/Engine/Functions/.../DitherTemporalAA) で確率的に抜けば
//   深度を書いたまま無加工版を使えるが、TemporalAA/TSR が前提でノイズが乗るうえ、
//   この組はエクスポーター自身が「見た目ほぼ不透明」と分類したものなので採らなかった。
//   本当に柔らかさが要る透け髪は alphaClass=="blend" 側 (Translucent) に来る。
// ===========================================================================
UTexture2D* FMmdMaterialConversion::FindImportedTextureByImageName(const FString& ImageName, const FString& PackagePath)
{
	return FindImportedTexture(ImageName, PackagePath);
}

bool FMmdMaterialConversion::MeasureUvAlpha(USkeletalMesh* Mesh, int32 SlotIndex, UTexture2D* Texture,
	FMmdUvAlphaStats& OutStats)
{
	OutStats = FMmdUvAlphaStats();
	if (Mesh == nullptr || Texture == nullptr) return false;

	// ★描画用ではなく**編集用モデル**の頂点を使う。描画用バッファは CPU 側の
	//   コピーが解放されていることがあり、UV を読めるとは限らない。
	const FSkeletalMeshModel* Model = Mesh->GetImportedModel();
	if (Model == nullptr || Model->LODModels.Num() == 0) return false;
	const FSkeletalMeshLODModel& LodModel = Model->LODModels[0];

	// ★テクスチャは「取り込んだ元画像」(Source) から読む。
	//   実行時のミップは圧縮 (BC) されていてアルファが量子化されるため、
	//   半透明の割合を測るには使えない。
	FTextureSource& Src = Texture->Source;
	if (!Src.IsValid()) return false;
	const ETextureSourceFormat Format = Src.GetFormat();
	if (Format != TSF_BGRA8)
	{
		// MMD のテクスチャは PNG/BMP なので通常 BGRA8。それ以外は測らずに諦める
		// (誤判定するより、昇格させないほうが安全)。
		UE_LOG(LogMmdPhysics, Verbose,
			TEXT("[MmdPhysics] '%s' のソース形式が BGRA8 ではないため UV 領域を測れません。"),
			*Texture->GetName());
		return false;
	}

	// PNG のまま保持されているソースもあるので、展開に ImageWrapper を渡す。
	IImageWrapperModule& ImageWrapper = FModuleManager::LoadModuleChecked<IImageWrapperModule>("ImageWrapper");
	TArray64<uint8> Mip;
	if (!Src.GetMipData(Mip, 0, &ImageWrapper)) return false;

	const int32 Width = Src.GetSizeX();
	const int32 Height = Src.GetSizeY();
	if (Width <= 0 || Height <= 0 || Mip.Num() < static_cast<int64>(Width) * Height * 4) return false;

	int32 Samples = 0, Semi = 0, Opaque = 0;
	double AlphaSum = 0.0;

	for (const FSkelMeshSection& Section : LodModel.Sections)
	{
		if (Section.MaterialIndex != SlotIndex) continue;

		// 頂点が多い材質は間引く。割合が分かればよいので全点は要らない。
		const int32 Num = Section.SoftVertices.Num();
		const int32 Stride = FMath::Max(1, Num / 4000);
		for (int32 i = 0; i < Num; i += Stride)
		{
			const FVector2f UV = Section.SoftVertices[i].UVs[0];
			// UV は 1 を超え得る (繰り返し)。小数部だけ取る。
			const float FracU = UV.X - FMath::FloorToFloat(UV.X);
			const float FracV = UV.Y - FMath::FloorToFloat(UV.Y);
			const int32 X = FMath::Clamp(static_cast<int32>(FracU * (Width - 1)), 0, Width - 1);
			const int32 Y = FMath::Clamp(static_cast<int32>(FracV * (Height - 1)), 0, Height - 1);

			const uint8 A = Mip[(static_cast<int64>(Y) * Width + X) * 4 + 3];  // BGRA
			const float Alpha = A / 255.0f;
			AlphaSum += Alpha;
			if (Alpha >= 0.98f) Opaque++;
			else if (Alpha > 0.02f) Semi++;
			Samples++;
		}
	}

	if (Samples == 0) return false;
	OutStats.Samples = Samples;
	OutStats.FracSemi = static_cast<float>(Semi) / Samples;
	OutStats.FracOpaque = static_cast<float>(Opaque) / Samples;
	OutStats.MeanAlpha = static_cast<float>(AlphaSum / Samples);
	return true;
}

FMmdMaterialPlan FMmdMaterialConversion::PlanMaterial(const MmdMaterialInfo& Info, const FMmdUvAlphaStats& UvStats)
{
	FMmdMaterialPlan Plan;

	const bool bBlendMode = IsBlendMaterial(Info);
	const bool bMaskMode = Info.AlphaMode.Equals(TEXT("MASK"), ESearchCase::IgnoreCase);

	// 真の半透明かどうかは alphaMode ではなく alphaClass にしか残っていない。
	// (alphaMode はプリベイク後の「ビューア安全な」値なので MASK に落ちていることがある)
	const bool bTrueBlend = bBlendMode || Info.IsAlphaClassBlend();

	const bool bHasOrigTexture = Info.OrigTexture >= 0;

	// ★肌に貼り付けた半透明の材質 (眉・まつげ・額の影・チーク) かどうか。
	//   MMD は 1 枚のテクスチャを不透明な肌と半透明の貼り付けで共有するので、
	//   材質が使う UV 領域のアルファ分布でしか区別できない。
	//   ★この判定が要る理由: 移植元 Unity 版は origTexture を持つ材質を一律に
	//     Transparent へ昇格させ、深度は lilToon の TwoPass が書いていた。UE の
	//     半透明は深度を書けないため一律には昇格できない (肌まで半透明にすると
	//     後ろ髪が顔を突き抜ける)。貼り付け材質は下地の肌が深度を書いてくれるので
	//     昇格させても安全で、これが「眉が硬く出る」問題の解になる。
	//
	//   ★半透明率だけでは足りない。2 モデルでの実測:
	//
	//     モデル 材質            半透明率 平均α  正体
	//     IA     mat4              29%    0.59  眉・まつげ      → 昇格させたい
	//     IA     顔3               21%    0.60  額の影          → 昇格させたい
	//     IA     mat1 / mat3      0-1%    1.00  肌              → 据え置き
	//     V4X    body_cloth        12%    0.99  服 (広い不透明面) → 据え置き
	//     V4X    face01            11%    0.90  顔              → 据え置き
	//     V4X    megane            46%    0.86  メガネのフレーム  → 据え置き
	//     V4X    hairshadow        65%    0.22  髪の影          → 昇格させてよい
	//     V4X    cheek             45%    0.08  チーク          → 昇格させてよい
	//
	//     半透明率だけだと V4X の服・顔・メガネのフレームを巻き込む。平均αを併用すると
	//     「薄い墨で描かれた貼り付け」と「縁だけ柔らかい不透明面」が分かれる。
	constexpr float KOverlaySemiFraction = 0.15f;
	constexpr float KOverlayMeanAlpha = 0.75f;
	Plan.bOverlay = bHasOrigTexture && UvStats.IsValid()
		&& UvStats.FracSemi >= KOverlaySemiFraction
		&& UvStats.MeanAlpha < KOverlayMeanAlpha;

	// 昇格は「無加工版があり、かつ 本来が真の半透明 or 貼り付け材質」のとき。
	// origTexture があるだけで一律に昇格させないのが移植元との違い。
	Plan.bPromotedByOrigTexture = bHasOrigTexture && (bTrueBlend || Plan.bOverlay) && !bBlendMode;
	Plan.bTranslucent = bBlendMode || Plan.bPromotedByOrigTexture;

	// ★無加工版があれば常に使う (移植元と同じ)。
	//
	//   プリベイク版は「アルファを 0/255 に平坦化しただけの版」ではなく、
	//   **半透明だった画素を下地と合成して明るくした版**だった (IA の 肌4.png で実測)。
	//     ・残る画素の集合 (シルエット) はプリベイク版と無加工版で完全に一致
	//     ・違うのは縁の画素の濃さだけ。半透明かつ暗い画素 (眉・目の輪郭の縁) で
	//       平均明度 92 (プリベイク) に対し 56 (無加工)、最大差 151
	//   つまり無加工版は**アルファブレンド専用**。硬いアルファテストで描くと
	//   縁の暗い画素がフルの濃さで出る (実機で眉・目の輪郭の黒フチとして現れた)。
	//   実際にブレンドできる材質 (Translucent) にだけ入れる。
	Plan.bUseOrigTexture = bHasOrigTexture && Plan.bTranslucent;

	// ★不透明サブパス (lilToon TwoPass 相当) を使うか。
	//   移植元 434 行の `if (modeInt == 2 && (hasBaseTex || useOrigTexture)) transparentMode = 2;`
	//   と同じ条件。テクスチャを持たない半透明 (レンズのような単色ガラス) は
	//   移植元も Normal のままなので、素のアルファブレンドへ倒す。
	Plan.bSubpassOpaque = Plan.bTranslucent && (Info.BaseColorTexture >= 0 || bHasOrigTexture);

	// alphaMode=OPAQUE はアルファを見ない (glTF の定義) ので負の値にする。
	// MASK はテクスチャのアルファでしきい値カット (移植元 491 行の _Cutoff に相当)。
	// ※以前はここを extras.mmd の alphaClass=="opaque" で判定しようとしていたが、
	//   alphaClass はプリベイク前の分類であって glTF のブレンド指定ではないので、
	//   しきい値の判断には glTF 側の alphaMode を使う。
	Plan.AlphaCutoff = bMaskMode ? Info.EffectiveAlphaCutoff() : -1.0f;

	return Plan;
}

UMaterial* FMmdMaterialConversion::EnsureMasterMaterial(const FString& PackagePath, EMmdMasterVariant Variant)
{
	// プラグイン側でグラフを作り替えたときに、古いマスターを掴み続けないようにする。
	// 版はマテリアル内の "MmdToonVersion" スカラーパラメータで持つ。
	// 7.0: Translucent 版マスターを追加し、グラフ生成を共通化 (2026-08-14)。
	// 8.0: BaseColor (glTF baseColorFactor = PMX の拡散色) を色とアルファに掛ける (2026-08-15)。
	// 9.0: 半透明に不透明サブパス (lilToon TwoPass 相当) を入れる (2026-08-15)。
	// 10.0: マスク閾値を BasePropertyOverrides からマテリアルのパラメータへ移した。
	//       (値ごとに静的 permutation が増えるのを避けるため) (2026-08-15)。
	// 11.0: 方式が決まったので検証用の分岐を削除。ディザと、プリベイク版/無加工版を
	//       切り替えるための 2 枚目のサンプラを捨てた (2026-08-15)。
	// 12.0: 輪郭線フラグ (UseOutline) を持たせた。描くのは MmdOutlineComponent (2026-08-15)。
	static constexpr float KMasterVersion = 12.0f;

	const bool bTranslucent = (Variant == EMmdMasterVariant::Translucent);
	const TCHAR* AssetName = MasterAssetName(Variant);

	const FString FullPath = PackagePath / AssetName + TEXT(".") + AssetName;
	if (UMaterial* Existing = LoadObject<UMaterial>(nullptr, *FullPath, nullptr, LOAD_NoWarn | LOAD_Quiet))
	{
		const float Ver = UMaterialEditingLibrary::GetMaterialDefaultScalarParameterValue(Existing, TEXT("MmdToonVersion"));
		if (FMath::IsNearlyEqual(Ver, KMasterVersion))
		{
			return Existing;
		}
		// 版が古い/壊れている。既存グラフを捨てて作り直す。
		UE_LOG(LogMmdPhysics, Log, TEXT("[MmdPhysics] 既存の %s を作り直します (版 %g → %g)。"),
			AssetName, Ver, KMasterVersion);
		Existing->GetEditorOnlyData()->ExpressionCollection.Empty();
	}

	UMaterial* Mat = LoadObject<UMaterial>(nullptr, *FullPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (Mat == nullptr)
	{
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
		UMaterialFactoryNew* Factory = NewObject<UMaterialFactoryNew>();
		Mat = Cast<UMaterial>(AssetTools.CreateAsset(AssetName, PackagePath, UMaterial::StaticClass(), Factory));
	}
	if (Mat == nullptr) return nullptr;

	// 版の印 (グラフには繋がない。再生成の判定にだけ使う)。
	{
		auto* VerParam = MakeNode<UMaterialExpressionScalarParameter>(Mat, -1500, -200);
		VerParam->ParameterName = TEXT("MmdToonVersion");
		VerParam->DefaultValue = KMasterVersion;
	}

	// MMD は KHR_materials_unlit で出力される。陰影はトゥーンランプで自前に作る。
	Mat->SetShadingModel(MSM_Unlit);

	// ★ブレンドモードはマテリアル単位の静的スイッチなので、alphaMode ごとにマスターを分ける。
	//   OPAQUE / MASK → Masked (opaque な材質はインスタンス側でマスク閾値を 0 にして実質不透明にする)
	//   BLEND         → Translucent (しきい値で切らず、アルファをそのまま混ぜる)
	//   以前は全材質を Masked に寄せていたが、IA の眉のような半透明グラデーションが
	//   しきい値で切り抜かれ、移植元 Unity 版と見た目が食い違っていた。
	Mat->BlendMode = bTranslucent ? BLEND_Translucent : BLEND_Masked;
	Mat->TwoSided = true;   // MMD は両面材質が多い。個別制御はインスタンスの上書きで行う。

	// ★使用フラグを必ず立てる。
	//   bUsedWithSkeletalMesh が無いと、UE はスケルタルメッシュ上でこのマテリアルを使えず
	//   既定マテリアル (灰色) で描画する。エディタでは自動で立てて警告を出すが、
	//   その状態のまま保存されていないとパッケージ後に色が出ない。
	//   MMD モデルは表情モーフを持つので bUsedWithMorphTargets も同時に立てる。
	{
		bool bNeedsRecompile = false;
		Mat->SetMaterialUsage(bNeedsRecompile, MATUSAGE_SkeletalMesh);
		Mat->SetMaterialUsage(bNeedsRecompile, MATUSAGE_MorphTargets);
	}

	// --- パラメータ ---
	// ★TextureSampleParameter には必ず既定テクスチャを入れること。
	//   null のままだとマテリアルのコンパイルが通らず、既定マテリアル (灰色) で表示される。
	//   白を既定にしておけば、パラメータ未設定の材質でも「素通し」として振る舞う。
	UTexture* DefaultWhite = LoadObject<UTexture>(nullptr, TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"));

	auto* BaseTex = MakeNode<UMaterialExpressionTextureSampleParameter2D>(Mat, -900, 0);
	BaseTex->ParameterName = TEXT("BaseColorTex");
	BaseTex->Texture = DefaultWhite;

	// ★BaseColorTex にはプリベイク版と無加工版のどちらかが入る (どちらを使うかは
	//   PlanMaterial が決める)。両方をサンプラで持って切り替える作りも試したが、
	//   materials は 16 サンプラの上限があり、常時 1 枚余計に使うのは割に合わない。
	//   判定を外したいときはインスタンスの BaseColorTex をもう一方のアセットへ
	//   差し替えればよい (無加工版も取り込み済みなので選べる)。

	// ★PMX の材質の拡散色 (glTF の baseColorFactor)。
	//   テクスチャを持たない材質 (レンズ・金属パーツ・単色のベルト) はここにしか色が無いので、
	//   これを落とすとその手の材質が既定の白テクスチャのまま真っ白に出る。
	//   アルファ側も掛ける (レンズの 0.7 のような MMD の半透明はこの値で決まる)。
	auto* BaseColor = MakeNode<UMaterialExpressionVectorParameter>(Mat, -900, 150);
	BaseColor->ParameterName = TEXT("BaseColor");
	BaseColor->DefaultValue = FLinearColor::White;

	auto* ToonTex = MakeNode<UMaterialExpressionTextureSampleParameter2D>(Mat, -900, 300);
	ToonTex->ParameterName = TEXT("ToonTex");
	ToonTex->Texture = DefaultWhite;

	auto* SphereTex = MakeNode<UMaterialExpressionTextureSampleParameter2D>(Mat, -900, 600);
	SphereTex->ParameterName = TEXT("SphereTex");
	SphereTex->Texture = DefaultWhite;

	auto* LightDir = MakeNode<UMaterialExpressionVectorParameter>(Mat, -1300, 200);
	LightDir->ParameterName = TEXT("LightDir");
	LightDir->DefaultValue = FLinearColor(-0.5f, -0.5f, 0.7f, 1.0f);

	auto* UseToon = MakeNode<UMaterialExpressionScalarParameter>(Mat, -600, 400);
	UseToon->ParameterName = TEXT("UseToon");
	UseToon->DefaultValue = 0.0f;

	auto* SphereMul = MakeNode<UMaterialExpressionScalarParameter>(Mat, -600, 700);
	SphereMul->ParameterName = TEXT("SphereMulWeight");
	SphereMul->DefaultValue = 0.0f;

	auto* SphereAdd = MakeNode<UMaterialExpressionScalarParameter>(Mat, -600, 800);
	SphereAdd->ParameterName = TEXT("SphereAddWeight");
	SphereAdd->DefaultValue = 0.0f;

	// 輪郭線 (エッジ) の値。ここでは描かない。
	// 描くのは別コンポーネント (MmdOutlineComponent) で、この 3 つを読んで
	// 輪郭線用のマテリアルへ渡す。
	auto* EdgeColor = MakeNode<UMaterialExpressionVectorParameter>(Mat, -1300, 900);
	EdgeColor->ParameterName = TEXT("EdgeColor");
	auto* EdgeSize = MakeNode<UMaterialExpressionScalarParameter>(Mat, -1300, 1000);
	EdgeSize->ParameterName = TEXT("EdgeSize");
	// PMX の描画フラグ bit4。輪郭線を描く材質かどうか。
	auto* UseOutline = MakeNode<UMaterialExpressionScalarParameter>(Mat, -1300, 1100);
	UseOutline->ParameterName = TEXT("UseOutline");
	UseOutline->DefaultValue = 0.0f;

	auto* One = MakeNode<UMaterialExpressionConstant>(Mat, -600, 300);
	One->R = 1.0f;
	auto* Half = MakeNode<UMaterialExpressionConstant>(Mat, -1100, 350);
	Half->R = 0.5f;

	// --- トゥーン: v = 1 - saturate(dot(N, LightDir)) で縦方向ランプを引く ---
	auto* NormalWS = MakeNode<UMaterialExpressionVertexNormalWS>(Mat, -1300, 100);
	auto* NdotL = MakeNode<UMaterialExpressionDotProduct>(Mat, -1150, 150);
	Connect(NormalWS, TEXT(""), NdotL, TEXT("A"));
	Connect(LightDir, TEXT(""), NdotL, TEXT("B"));

	// ★Saturate / OneMinus / Transform の入力ピンは「名前が空」。
	//   "Input" を指定すると接続に失敗する (例外は出ず、黙って未接続のグラフができる)。
	auto* NdotLSat = MakeNode<UMaterialExpressionSaturate>(Mat, -1050, 150);
	Connect(NdotL, TEXT(""), NdotLSat, TEXT(""));

	auto* ToonV = MakeNode<UMaterialExpressionOneMinus>(Mat, -1000, 150);
	Connect(NdotLSat, TEXT(""), ToonV, TEXT(""));

	auto* ToonUV = MakeNode<UMaterialExpressionAppendVector>(Mat, -950, 250);
	Connect(Half, TEXT(""), ToonUV, TEXT("A"));
	Connect(ToonV, TEXT(""), ToonUV, TEXT("B"));
	Connect(ToonUV, TEXT(""), ToonTex, TEXT("UVs"));

	// トゥーンを使わない材質では 1 (無影響) にする。
	auto* ToonFactor = MakeNode<UMaterialExpressionLinearInterpolate>(Mat, -450, 350);
	Connect(One, TEXT(""), ToonFactor, TEXT("A"));
	Connect(ToonTex, TEXT("RGB"), ToonFactor, TEXT("B"));
	Connect(UseToon, TEXT(""), ToonFactor, TEXT("Alpha"));

	// --- スフィアマップ: ビュー空間法線の XY を UV にする ---
	auto* NormalVS = MakeNode<UMaterialExpressionTransform>(Mat, -1150, 600);
	NormalVS->TransformSourceType = TRANSFORMSOURCE_World;
	NormalVS->TransformType = TRANSFORM_View;
	Connect(NormalWS, TEXT(""), NormalVS, TEXT(""));

	auto* SphereUvScaled = MakeNode<UMaterialExpressionMultiply>(Mat, -1050, 600);
	Connect(NormalVS, TEXT(""), SphereUvScaled, TEXT("A"));
	Connect(Half, TEXT(""), SphereUvScaled, TEXT("B"));

	auto* SphereUv = MakeNode<UMaterialExpressionAdd>(Mat, -980, 600);
	Connect(SphereUvScaled, TEXT(""), SphereUv, TEXT("A"));
	Connect(Half, TEXT(""), SphereUv, TEXT("B"));

	// ★UV は float2。ビュー空間法線は float3 なので、そのまま繋ぐと
	//   「Cannot cast from larger type float3 to smaller type float2」で
	//   マテリアル全体のコンパイルが落ち、既定マテリアル (灰色) で描画される。
	//   RG 成分だけ取り出して渡すこと。
	auto* SphereUvXY = MakeNode<UMaterialExpressionComponentMask>(Mat, -930, 600);
	SphereUvXY->R = true;
	SphereUvXY->G = true;
	SphereUvXY->B = false;
	SphereUvXY->A = false;
	Connect(SphereUv, TEXT(""), SphereUvXY, TEXT(""));
	Connect(SphereUvXY, TEXT(""), SphereTex, TEXT("UVs"));

	// 乗算スフィア: 使わない材質では 1。
	auto* SphereMulFactor = MakeNode<UMaterialExpressionLinearInterpolate>(Mat, -450, 650);
	Connect(One, TEXT(""), SphereMulFactor, TEXT("A"));
	Connect(SphereTex, TEXT("RGB"), SphereMulFactor, TEXT("B"));
	Connect(SphereMul, TEXT(""), SphereMulFactor, TEXT("Alpha"));

	// 加算スフィア: 使わない材質では 0。
	auto* SphereAddColor = MakeNode<UMaterialExpressionMultiply>(Mat, -450, 800);
	Connect(SphereTex, TEXT("RGB"), SphereAddColor, TEXT("A"));
	Connect(SphereAdd, TEXT(""), SphereAddColor, TEXT("B"));

	// --- 合成: Base * BaseColor * Toon * SphereMul + SphereAdd ---
	auto* Tinted = MakeNode<UMaterialExpressionMultiply>(Mat, -380, 60);
	Connect(BaseTex, TEXT("RGB"), Tinted, TEXT("A"));
	Connect(BaseColor, TEXT(""), Tinted, TEXT("B"));

	auto* Lit = MakeNode<UMaterialExpressionMultiply>(Mat, -300, 100);
	Connect(Tinted, TEXT(""), Lit, TEXT("A"));
	Connect(ToonFactor, TEXT(""), Lit, TEXT("B"));

	auto* WithSphereMul = MakeNode<UMaterialExpressionMultiply>(Mat, -200, 200);
	Connect(Lit, TEXT(""), WithSphereMul, TEXT("A"));
	Connect(SphereMulFactor, TEXT(""), WithSphereMul, TEXT("B"));

	auto* Final = MakeNode<UMaterialExpressionAdd>(Mat, -120, 300);
	Connect(WithSphereMul, TEXT(""), Final, TEXT("A"));
	Connect(SphereAddColor, TEXT(""), Final, TEXT("B"));

	if (!UMaterialEditingLibrary::ConnectMaterialProperty(Final, TEXT(""), MP_EmissiveColor))
	{
		UE_LOG(LogMmdPhysics, Error, TEXT("[MmdPhysics] EmissiveColor への接続に失敗しました (色が出ません)。"));
	}
	// アルファもテクスチャ × 拡散色のアルファ。テクスチャを持たない半透明材質
	// (レンズ = 拡散色アルファ 0.7) は、既定の白テクスチャのアルファ 1 との積で 0.7 になる。
	auto* AlphaMul = MakeNode<UMaterialExpressionMultiply>(Mat, -300, 0);
	Connect(BaseTex, TEXT("A"), AlphaMul, TEXT("A"));
	Connect(BaseColor, TEXT("A"), AlphaMul, TEXT("B"));

	// ★半透明の「不透明サブパス」(移植元 lilToon の TwoPass 相当)。
	//
	//   lilToon の TwoPass は 2 パス構成で、1 パス目が
	//     clip(alpha - _SubpassCutoff);   // _SubpassCutoff の既定は 0.5
	//   を ZWrite あり・フルカラーマスクで描き (lts_twotrans.shader の _PreZWrite=1 /
	//   _PreColorMask=15、SubShader のキューも "AlphaTest+10")、2 パス目でブレンドする。
	//   つまり **α >= 0.5 の部分は完全な不透明として描かれる**。
	//
	//   UE のマテリアルは 1 パスしか持てないので、同じ見え方を不透明度の付け替えで作る:
	//     α >= Cutoff → 1.0 (サブパスが不透明で描いた分)
	//     α <  Cutoff → α   (ブレンドパスの分)
	//   1 層なら lilToon と同じ結果になる (サブパスが塗った色の上に同じ色を混ぜても変わらないため)。
	//
	//   これを入れないと、髪のように α 0.75〜1.0 の帯が広いテクスチャが一様に透けてしまい、
	//   「前髪ごしに眉が透けすぎる」形で移植元と食い違う (IA で実測: 前髪は 98.4% が α>=0.75)。
	UMaterialExpression* AlphaOut = AlphaMul;
	if (!bTranslucent)
	{
		// ==============================================================
		// Masked 側のアルファ。しきい値は **マテリアルのパラメータ** で持つ。
		//
		// ★UE の OpacityMaskClipValue はインスタンスの BasePropertyOverrides なので、
		//   値ごとに静的 permutation が増える。マスク判定を自前で作り、
		//   マテリアルの OpacityMaskClipValue は 0.5 固定にして
		//   「出力 > 0.5 なら描く」という約束にそろえてある。
		//
		//   RawA - AlphaCutoff + 0.5  → RawA > AlphaCutoff で描く
		//   (AlphaCutoff が負なら常に描く = glTF の alphaMode=OPAQUE)
		// ==============================================================
		auto* CutoffParam = MakeNode<UMaterialExpressionScalarParameter>(Mat, -600, -60);
		CutoffParam->ParameterName = TEXT("AlphaCutoff");
		CutoffParam->DefaultValue = 0.5f;

		auto* HalfConst = MakeNode<UMaterialExpressionConstant>(Mat, -600, -120);
		HalfConst->R = 0.5f;

		auto* HardSub = MakeNode<UMaterialExpressionSubtract>(Mat, -260, -60);
		Connect(AlphaMul, TEXT(""), HardSub, TEXT("A"));
		Connect(CutoffParam, TEXT(""), HardSub, TEXT("B"));
		auto* HardMask = MakeNode<UMaterialExpressionAdd>(Mat, -230, -60);
		Connect(HardSub, TEXT(""), HardMask, TEXT("A"));
		Connect(HalfConst, TEXT(""), HardMask, TEXT("B"));
		AlphaOut = HardMask;

		Mat->OpacityMaskClipValue = 0.5f;

		// ★ここに「ディザで縁を柔らかくする」経路も作って試したが、顔のような
		//   近距離の面では市松模様がそのまま見えたため捨てた (実機で確認)。
		//   移植元 lilToon の TwoPass (深度を書きつつ縁は柔らかい) に構造的には
		//   一致するので発想としては筋が良いが、絵として使えない。
		//   代わりに Masked 側はプリベイク版テクスチャを使う。プリベイク版は
		//   縁の画素が下地と合成済みで、アルファテストで描く前提に作られている。
	}
	else
	{
		auto* SubpassCutoff = MakeNode<UMaterialExpressionScalarParameter>(Mat, -600, 60);
		SubpassCutoff->ParameterName = TEXT("SubpassCutoff");
		SubpassCutoff->DefaultValue = 0.5f;   // lilToon の _SubpassCutoff 既定値

		// テクスチャを持たない半透明 (レンズのような単色ガラス) は移植元も TwoPass にしない。
		// その場合はインスタンス側で 0 にして、素のアルファブレンドへ倒す。
		auto* SubpassWeight = MakeNode<UMaterialExpressionScalarParameter>(Mat, -600, 120);
		SubpassWeight->ParameterName = TEXT("SubpassWeight");
		SubpassWeight->DefaultValue = 0.0f;

		auto* Diff = MakeNode<UMaterialExpressionSubtract>(Mat, -260, 60);
		Connect(AlphaMul, TEXT(""), Diff, TEXT("A"));
		Connect(SubpassCutoff, TEXT(""), Diff, TEXT("B"));

		// ceil(saturate(α - cutoff)) → α > cutoff で 1、それ以外は 0。
		auto* DiffSat = MakeNode<UMaterialExpressionSaturate>(Mat, -230, 60);
		Connect(Diff, TEXT(""), DiffSat, TEXT(""));
		auto* OpaqueMask = MakeNode<UMaterialExpressionCeil>(Mat, -200, 60);
		Connect(DiffSat, TEXT(""), OpaqueMask, TEXT(""));

		auto* Stepped = MakeNode<UMaterialExpressionAdd>(Mat, -170, 60);
		Connect(AlphaMul, TEXT(""), Stepped, TEXT("A"));
		Connect(OpaqueMask, TEXT(""), Stepped, TEXT("B"));
		auto* SteppedSat = MakeNode<UMaterialExpressionSaturate>(Mat, -140, 60);
		Connect(Stepped, TEXT(""), SteppedSat, TEXT(""));

		auto* AlphaFinal = MakeNode<UMaterialExpressionLinearInterpolate>(Mat, -110, 60);
		Connect(AlphaMul, TEXT(""), AlphaFinal, TEXT("A"));
		Connect(SteppedSat, TEXT(""), AlphaFinal, TEXT("B"));
		Connect(SubpassWeight, TEXT(""), AlphaFinal, TEXT("Alpha"));
		AlphaOut = AlphaFinal;
	}

	// ★アルファの行き先はブレンドモードで変わる。Masked は OpacityMask (しきい値カット)、
	//   Translucent は Opacity (そのまま混ぜる)。繋ぐ先を間違えても接続自体は成功してしまい、
	//   「半透明が全部不透明」「マスクが効かない」という形でしか気付けない。
	const EMaterialProperty AlphaTarget = bTranslucent ? MP_Opacity : MP_OpacityMask;
	if (!UMaterialEditingLibrary::ConnectMaterialProperty(AlphaOut, TEXT(""), AlphaTarget))
	{
		UE_LOG(LogMmdPhysics, Error, TEXT("[MmdPhysics] %s への接続に失敗しました。"),
			bTranslucent ? TEXT("Opacity") : TEXT("OpacityMask"));
	}

	UMaterialEditingLibrary::RecompileMaterial(Mat);

	// ★コンパイル結果を必ず検査する。
	//   マテリアルのコンパイルが落ちても例外は出ず、描画時に既定マテリアル (灰色) へ
	//   差し替わるだけなので、目視でしか気付けない。ここで落として原因を名指しする。
	//   (実例: ビュー空間法線 float3 を UV(float2) へ繋いで
	//    「Cannot cast from larger type float3 to smaller type float2」)
	if (const FMaterialResource* Res = Mat->GetMaterialResource(GMaxRHIFeatureLevel))
	{
		const TArray<FString>& Errors = Res->GetCompileErrors();
		if (Errors.Num() > 0)
		{
			for (const FString& E : Errors)
			{
				UE_LOG(LogMmdPhysics, Error, TEXT("[MmdPhysics] マテリアルのコンパイルエラー: %s"), *E);
			}
			return nullptr;
		}
	}

	SavePackageOfAsset(Mat);

	UE_LOG(LogMmdPhysics, Log, TEXT("[MmdPhysics] マスターマテリアル (%s) を生成しました: %s"),
		bTranslucent ? TEXT("Translucent") : TEXT("Masked"), *FullPath);
	return Mat;
}

UMaterial* FMmdMaterialConversion::EnsureOutlineMaterial(const FString& PackagePath)
{
	// 輪郭線マスターの版。本体マスターとは別に持つ (グラフが別物なので)。
	// 2.0: エッジ色のアルファを効かせるため Masked → Translucent (2026-08-15)。
	static constexpr float KOutlineVersion = 2.0f;

	const FString FullPath = PackagePath / KOutlineName + TEXT(".") + KOutlineName;
	if (UMaterial* Existing = LoadObject<UMaterial>(nullptr, *FullPath, nullptr, LOAD_NoWarn | LOAD_Quiet))
	{
		const float Ver = UMaterialEditingLibrary::GetMaterialDefaultScalarParameterValue(Existing, TEXT("MmdOutlineVersion"));
		if (FMath::IsNearlyEqual(Ver, KOutlineVersion))
		{
			return Existing;
		}
		UE_LOG(LogMmdPhysics, Log, TEXT("[MmdPhysics] 既存の %s を作り直します (版 %g → %g)。"),
			KOutlineName, Ver, KOutlineVersion);
		Existing->GetEditorOnlyData()->ExpressionCollection.Empty();
	}

	UMaterial* Mat = LoadObject<UMaterial>(nullptr, *FullPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (Mat == nullptr)
	{
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
		UMaterialFactoryNew* Factory = NewObject<UMaterialFactoryNew>();
		Mat = Cast<UMaterial>(AssetTools.CreateAsset(KOutlineName, PackagePath, UMaterial::StaticClass(), Factory));
	}
	if (Mat == nullptr) return nullptr;

	{
		auto* VerParam = MakeNode<UMaterialExpressionScalarParameter>(Mat, -1500, -200);
		VerParam->ParameterName = TEXT("MmdOutlineVersion");
		VerParam->DefaultValue = KOutlineVersion;
	}

	// ===================================================================
	// MMD の輪郭線は「モデルをもう一度、法線方向へ膨らませて、表面を捨てて描く」
	// (反転ハル)。MMD 自身も同じ描き方をしている。
	//
	// UE では次のように作る:
	//   ・膨らませ  … WorldPositionOffset に 法線 × 太さ
	//   ・表面を捨てる … TwoSidedSign が +1 (表) の画素を Opacity 0 で落とす。
	//                    残るのは膨らませた裏面 = シルエットの輪
	//   ・色       … EmissiveColor に EdgeColor (Unlit なので陰影は付かない)
	//   ・濃さ     … Opacity に EdgeColor のアルファ
	//
	// ★Translucent なのは **PMX のエッジ色がアルファを持つ**ため (IA は 0.6〜1.0)。
	//   MMD はここをブレンドするので、Masked で不透明に描くと輪郭線が濃く出る。
	//   半透明なので深度は書かないが、本体は不透明パスで深度を書いており、
	//   輪郭線は膨らませた分だけ本体の外側にあるので前後関係は保たれる。
	//   ★輪郭線は本体の半透明 (髪など) より先に描く必要があるので、
	//     コンポーネント側で TranslucencySortPriority を下げている。
	//
	// ★メッシュは複製しない。同じスケルタルメッシュを参照する別コンポーネントに
	//   このマテリアルを割り当てる (MmdOutlineComponent)。セクションを複製する方式だと
	//   モーフのデルタまで作り直すことになり、しかも再インポートで消える。
	// ===================================================================
	Mat->SetShadingModel(MSM_Unlit);
	Mat->BlendMode = BLEND_Translucent;
	Mat->TwoSided = true;   // 裏面を描くので必須
	{
		bool bNeedsRecompile = false;
		Mat->SetMaterialUsage(bNeedsRecompile, MATUSAGE_SkeletalMesh);
		Mat->SetMaterialUsage(bNeedsRecompile, MATUSAGE_MorphTargets);
	}

	auto* EdgeColor = MakeNode<UMaterialExpressionVectorParameter>(Mat, -600, 0);
	EdgeColor->ParameterName = TEXT("EdgeColor");
	EdgeColor->DefaultValue = FLinearColor::Black;

	auto* EdgeSize = MakeNode<UMaterialExpressionScalarParameter>(Mat, -900, 200);
	EdgeSize->ParameterName = TEXT("EdgeSize");
	EdgeSize->DefaultValue = 1.0f;

	// ★PMX の edgeSize (IA は 0.596) を UE の cm へ落とす係数。
	//   MMD 側の見た目に合わせて実測で決めるものなので、パラメータで出しておく。
	auto* WidthScale = MakeNode<UMaterialExpressionScalarParameter>(Mat, -900, 260);
	WidthScale->ParameterName = TEXT("OutlineWidthScale");
	WidthScale->DefaultValue = 0.15f;

	auto* UseOutline = MakeNode<UMaterialExpressionScalarParameter>(Mat, -900, 400);
	UseOutline->ParameterName = TEXT("UseOutline");
	UseOutline->DefaultValue = 1.0f;

	// --- 膨らませ: 法線 × (EdgeSize × 係数) ---
	auto* Width = MakeNode<UMaterialExpressionMultiply>(Mat, -700, 230);
	Connect(EdgeSize, TEXT(""), Width, TEXT("A"));
	Connect(WidthScale, TEXT(""), Width, TEXT("B"));

	auto* NormalWS = MakeNode<UMaterialExpressionVertexNormalWS>(Mat, -700, 150);
	auto* Offset = MakeNode<UMaterialExpressionMultiply>(Mat, -500, 200);
	Connect(NormalWS, TEXT(""), Offset, TEXT("A"));
	Connect(Width, TEXT(""), Offset, TEXT("B"));

	// --- 表面を捨てる: TwoSidedSign は表で +1 / 裏で -1 ---
	auto* Sign = MakeNode<UMaterialExpressionTwoSidedSign>(Mat, -700, 400);
	auto* NegSign = MakeNode<UMaterialExpressionMultiply>(Mat, -600, 400);
	auto* MinusOne = MakeNode<UMaterialExpressionConstant>(Mat, -700, 460);
	MinusOne->R = -1.0f;
	Connect(Sign, TEXT(""), NegSign, TEXT("A"));
	Connect(MinusOne, TEXT(""), NegSign, TEXT("B"));

	auto* BackOnly = MakeNode<UMaterialExpressionSaturate>(Mat, -500, 400);
	Connect(NegSign, TEXT(""), BackOnly, TEXT(""));

	// 輪郭線を描かない材質はここで全部落とす。
	auto* MaskOut = MakeNode<UMaterialExpressionMultiply>(Mat, -400, 400);
	Connect(BackOnly, TEXT(""), MaskOut, TEXT("A"));
	Connect(UseOutline, TEXT(""), MaskOut, TEXT("B"));

	// PMX のエッジ色はアルファを持つ (IA は 0.6〜1.0)。MMD はこれでブレンドする。
	auto* Opacity = MakeNode<UMaterialExpressionMultiply>(Mat, -300, 400);
	Connect(MaskOut, TEXT(""), Opacity, TEXT("A"));
	Connect(EdgeColor, TEXT("A"), Opacity, TEXT("B"));

	if (!UMaterialEditingLibrary::ConnectMaterialProperty(EdgeColor, TEXT(""), MP_EmissiveColor))
	{
		UE_LOG(LogMmdPhysics, Error, TEXT("[MmdPhysics] 輪郭線: EmissiveColor への接続に失敗しました。"));
	}
	if (!UMaterialEditingLibrary::ConnectMaterialProperty(Opacity, TEXT(""), MP_Opacity))
	{
		UE_LOG(LogMmdPhysics, Error, TEXT("[MmdPhysics] 輪郭線: Opacity への接続に失敗しました。"));
	}
	if (!UMaterialEditingLibrary::ConnectMaterialProperty(Offset, TEXT(""), MP_WorldPositionOffset))
	{
		UE_LOG(LogMmdPhysics, Error, TEXT("[MmdPhysics] 輪郭線: WorldPositionOffset への接続に失敗しました。"));
	}

	UMaterialEditingLibrary::RecompileMaterial(Mat);

	if (const FMaterialResource* Res = Mat->GetMaterialResource(GMaxRHIFeatureLevel))
	{
		const TArray<FString>& Errors = Res->GetCompileErrors();
		if (Errors.Num() > 0)
		{
			for (const FString& E : Errors)
			{
				UE_LOG(LogMmdPhysics, Error, TEXT("[MmdPhysics] 輪郭線マテリアルのコンパイルエラー: %s"), *E);
			}
			return nullptr;
		}
	}

	SavePackageOfAsset(Mat);
	UE_LOG(LogMmdPhysics, Log, TEXT("[MmdPhysics] 輪郭線マスターを生成しました: %s"), *FullPath);
	return Mat;
}

FMmdMaterialResult FMmdMaterialConversion::ConvertMaterials(USkeletalMesh* Mesh, const FString& GlbPath)
{
	FMmdMaterialResult Result;

	if (Mesh == nullptr)
	{
		Result.Message = TEXT("スケルタルメッシュが指定されていません。");
		return Result;
	}

	MmdMaterialSet Set;
	TArray<FString> Warnings;
	if (!GlbMaterialReader::LoadFile(GlbPath, Set, Warnings))
	{
		for (const FString& W : Warnings) UE_LOG(LogMmdPhysics, Warning, TEXT("[MmdPhysics] %s"), *W);
		Result.Message = Warnings.Num() > 0 ? Warnings[0] : TEXT(".glb からマテリアル情報を読めません。");
		return Result;
	}
	for (const FString& W : Warnings) UE_LOG(LogMmdPhysics, Warning, TEXT("[MmdPhysics] %s"), *W);

	const FString PackagePath = FPackageName::GetLongPackagePath(Mesh->GetOutermost()->GetName());
	UMaterial* MaskedMaster = EnsureMasterMaterial(PackagePath, EMmdMasterVariant::Masked);
	if (MaskedMaster == nullptr)
	{
		Result.Message = TEXT("マスターマテリアルを用意できませんでした。");
		return Result;
	}

	// Translucent マスターは半透明として描く材質が出てきたときにだけ作る
	// (半透明を持たないモデルのフォルダに未使用アセットを増やさない)。
	// 昇格するかどうかは UV 領域の測定を経ないと決まらないので、遅延生成にしてある。
	UMaterial* TranslucentMaster = nullptr;
	bool bTriedTranslucentMaster = false;
	auto GetTranslucentMaster = [&]() -> UMaterial*
	{
		if (!bTriedTranslucentMaster)
		{
			bTriedTranslucentMaster = true;
			TranslucentMaster = EnsureMasterMaterial(PackagePath, EMmdMasterVariant::Translucent);
			if (TranslucentMaster == nullptr)
			{
				// 致命ではない。Masked へ倒して続行する (従来どおりしきい値で抜ける)。
				UE_LOG(LogMmdPhysics, Warning,
					TEXT("[MmdPhysics] Translucent マスターを用意できませんでした。")
					TEXT("半透明の材質も Masked で変換します (半透明がしきい値で切り抜かれます)。"));
			}
		}
		return TranslucentMaster;
	};

	// テクスチャの引き当て。名前で見つからないもの (toon / sphere / origTexture) は
	// GLB バイナリから取り出す。キャッシュを持つのでモデル 1 体につき 1 個作る。
	FTextureResolver Textures(Set, PackagePath, GlbPath);

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	TArray<FSkeletalMaterial>& Slots = Mesh->GetMaterials();
	Result.Total = Slots.Num();

	Mesh->Modify();

	for (int32 SlotIndex = 0; SlotIndex < Slots.Num(); SlotIndex++)
	{
		// スロット名 (= 取り込まれたマテリアル名) で extras.mmd 側の材質を引き当てる。
		const FString SlotName = Slots[SlotIndex].MaterialSlotName.ToString();
		const MmdMaterialInfo* Info = Set.Materials.FindByPredicate(
			[&SlotName](const MmdMaterialInfo& M) { return M.Name == SlotName; });

		if (Info == nullptr && Set.Materials.IsValidIndex(SlotIndex))
		{
			// 名前で引けないときは順番で対応させる (Interchange が名前を変えた場合の保険)。
			Info = &Set.Materials[SlotIndex];
		}
		if (Info == nullptr)
		{
			UE_LOG(LogMmdPhysics, Warning, TEXT("[MmdPhysics] スロット '%s' に対応する材質が見つかりません。"), *SlotName);
			continue;
		}

		// ★テクスチャの引き当てを先に済ませる。
		//   無加工版が無いと「肌に貼り付けた半透明の材質か」を測れず、方針が決まらない。
		UTexture2D* PrebakedTexture = Textures.FindOrExtract(Info->BaseColorTexture);
		UTexture2D* OrigTexture = (Info->OrigTexture >= 0)
			? Textures.FindOrExtract(Info->OrigTexture) : nullptr;

		FMmdUvAlphaStats UvStats;
		if (OrigTexture != nullptr)
		{
			MeasureUvAlpha(Mesh, SlotIndex, OrigTexture, UvStats);
		}

		// 変換方針 (親マスター / origTexture の使用 / マスクしきい値) を決める。
		const FMmdMaterialPlan Plan = PlanMaterial(*Info, UvStats);
		UMaterial* const WantedMaster = Plan.bTranslucent ? GetTranslucentMaster() : MaskedMaster;
		const bool bTranslucent = Plan.bTranslucent && WantedMaster != nullptr;
		UMaterial* Master = bTranslucent ? WantedMaster : MaskedMaster;

		if (Plan.bOverlay)
		{
			UE_LOG(LogMmdPhysics, Log,
				TEXT("[MmdPhysics] '%s' は肌に貼り付けた半透明の材質と判定しました ")
				TEXT("(半透明 %.0f%% / 不透明 %.0f%% / 平均α %.2f)。半透明として描きます。"),
				*SlotName, UvStats.FracSemi * 100.0f, UvStats.FracOpaque * 100.0f, UvStats.MeanAlpha);
		}

		const FString MiName = FString::Printf(TEXT("MI_%s_%s"), *Mesh->GetName(), *SlotName);
		UMaterialInstanceConstant* MI = LoadObject<UMaterialInstanceConstant>(
			nullptr, *(PackagePath / MiName + TEXT(".") + MiName));
		if (MI == nullptr)
		{
			auto* MiFactory = NewObject<UMaterialInstanceConstantFactoryNew>();
			MiFactory->InitialParent = Master;
			MI = Cast<UMaterialInstanceConstant>(
				AssetTools.CreateAsset(MiName, PackagePath, UMaterialInstanceConstant::StaticClass(), MiFactory));
		}
		if (MI == nullptr) continue;

		// ★再変換で親が変わることがある (Masked 一本だった頃に作った MI、alphaMode の付いた
		//   .glb へ差し替えた場合など)。親を直さないと古いブレンドモードのまま残る。
		if (MI->Parent != Master)
		{
			MI->SetParentEditorOnly(Master);
		}

		// --- ベースカラー ---
		// ★origTexture (プリベイク前の無加工版) があればそちらを使う。
		//   エクスポーターはビューア安全のため α を 0/255 に平坦化した版を
		//   baseColorTexture に置いており、MMD 本来の柔らかい眉・透け髪・目の縁取りは
		//   無加工版にしか残っていない (移植元 409〜415 行の注記)。
		//   無加工版は標準マテリアルから参照されないので、Interchange では取り込まれず
		//   FTextureResolver の抽出経路に乗る。
		// ★アルファテストで描く材質にはプリベイク版、実際にブレンドする材質には無加工版。
		//   プリベイク版は縁の画素が下地と合成済みで、アルファテストで描く前提に
		//   作られている (エクスポーターの _prebake_mask_alpha)。無加工版は合成前なので、
		//   アルファテストで描くと縁の暗い画素がフルの濃さで出る。
		UTexture2D* BaseTexture = PrebakedTexture;
		if (Plan.bUseOrigTexture)
		{
			if (OrigTexture != nullptr)
			{
				BaseTexture = OrigTexture;
				Result.OrigTextureApplied++;
			}
			else
			{
				// 移植元と同じフォールバック。プリベイク版のまま続行する。
				UE_LOG(LogMmdPhysics, Warning,
					TEXT("[MmdPhysics] '%s': origTexture=%d の取り出しに失敗しました。")
					TEXT("プリベイク版のまま続行します。"),
					*SlotName, Info->OrigTexture);
			}
		}
		if (BaseTexture != nullptr)
		{
			UMaterialEditingLibrary::SetMaterialInstanceTextureParameterValue(MI, TEXT("BaseColorTex"), BaseTexture);
		}

		// ★拡散色 (移植元 462〜463 行の _Color)。テクスチャ無しの材質はここにしか色が無い。
		//   IA では mat11 (黒いベルト)・銀・髪止め・レンズ・mat7 の 5 材質が該当し、
		//   これを落とすと全部が既定の白テクスチャのまま真っ白に出る。
		UMaterialEditingLibrary::SetMaterialInstanceVectorParameterValue(MI, TEXT("BaseColor"),
			FLinearColor(Info->BaseColorFactor[0], Info->BaseColorFactor[1],
				Info->BaseColorFactor[2], Info->BaseColorFactor[3]));

		// トゥーン: 個別テクスチャがあればそれ。共有トゥーン (toon01..toon10) は GLB に入っていないので
		// プロジェクト内をファイル名で探す (Unity 版と同じ制約。ユーザーが別途用意する)。
		bool bHasToon = false;
		if (Info->ToonTexture >= 0)
		{
			if (UTexture2D* T = Textures.FindOrExtract(Info->ToonTexture))
			{
				UMaterialEditingLibrary::SetMaterialInstanceTextureParameterValue(MI, TEXT("ToonTex"), T);
				bHasToon = true;
			}
			else
			{
				UE_LOG(LogMmdPhysics, Warning,
					TEXT("[MmdPhysics] '%s': toonTexture=%d を取り出せませんでした。"),
					*SlotName, Info->ToonTexture);
			}
		}
		else if (Info->ToonShared >= 0)
		{
			const FString SharedName = FString::Printf(TEXT("toon%02d"), Info->ToonShared + 1);
			if (UTexture2D* T = FindImportedTexture(SharedName, PackagePath))
			{
				UMaterialEditingLibrary::SetMaterialInstanceTextureParameterValue(MI, TEXT("ToonTex"), T);
				bHasToon = true;
			}
			else
			{
				// 共有トゥーンはモデルに同梱されない。無いことに気付けるよう名指しで出す
				// (移植元 547 行の警告に相当)。
				UE_LOG(LogMmdPhysics, Warning,
					TEXT("[MmdPhysics] '%s': 共有トゥーン '%s' がプロジェクトに見つかりません。")
					TEXT("MMD 標準の toon01〜toon10 を別途取り込んでください (置き場所は自由)。"),
					*SlotName, *SharedName);
			}
		}
		UMaterialEditingLibrary::SetMaterialInstanceScalarParameterValue(MI, TEXT("UseToon"), bHasToon ? 1.0f : 0.0f);

		// --- スフィア ---
		float MulWeight = 0.0f, AddWeight = 0.0f;
		if (Info->SphereTexture >= 0 && Info->SphereMode == 3)
		{
			// mode 3 (サブテクスチャ) は UV1 で貼る追加テクスチャで、視線方向で
			// サンプリングするスフィアとは仕組みが違う。誤って乗算すると崩れるので触らない
			// (移植元 552〜558 行と同じ判断)。
			UE_LOG(LogMmdPhysics, Warning,
				TEXT("[MmdPhysics] '%s': sphereMode=3 (サブテクスチャ) は未対応のため適用しません。"), *SlotName);
		}
		else if (Info->SphereTexture >= 0 && Info->SphereMode > 0)
		{
			if (UTexture2D* T = Textures.FindOrExtract(Info->SphereTexture))
			{
				UMaterialEditingLibrary::SetMaterialInstanceTextureParameterValue(MI, TEXT("SphereTex"), T);
				if (Info->SphereMode == 1) MulWeight = 1.0f;       // 乗算
				else if (Info->SphereMode == 2) AddWeight = 1.0f;  // 加算
			}
			else
			{
				UE_LOG(LogMmdPhysics, Warning,
					TEXT("[MmdPhysics] '%s': sphereTexture=%d を取り出せませんでした。"),
					*SlotName, Info->SphereTexture);
			}
		}
		UMaterialEditingLibrary::SetMaterialInstanceScalarParameterValue(MI, TEXT("SphereMulWeight"), MulWeight);
		UMaterialEditingLibrary::SetMaterialInstanceScalarParameterValue(MI, TEXT("SphereAddWeight"), AddWeight);

		// --- 輪郭線 (描くのは MmdOutlineComponent。ここでは値だけ持たせる) ---
		UMaterialEditingLibrary::SetMaterialInstanceVectorParameterValue(MI, TEXT("EdgeColor"),
			FLinearColor(Info->EdgeColor[0], Info->EdgeColor[1], Info->EdgeColor[2], Info->EdgeColor[3]));
		UMaterialEditingLibrary::SetMaterialInstanceScalarParameterValue(MI, TEXT("EdgeSize"), Info->EdgeSize);
		// PMX 描画フラグ bit4。
		UMaterialEditingLibrary::SetMaterialInstanceScalarParameterValue(MI, TEXT("UseOutline"),
			Info->HasEdge() ? 1.0f : 0.0f);
		if (Info->HasEdge()) Result.WithOutline++;

		// --- 描画設定の上書き ---
		MI->BasePropertyOverrides.bOverride_TwoSided = true;
		MI->BasePropertyOverrides.TwoSided = Info->bDoubleSided || Info->IsDoubleSidedFlag();

		// --- アルファの描き方 (親によって効くパラメータが違う) ---
		if (bTranslucent)
		{
			// 不透明サブパス (lilToon TwoPass 相当)。
			UMaterialEditingLibrary::SetMaterialInstanceScalarParameterValue(MI, TEXT("SubpassWeight"),
				Plan.bSubpassOpaque ? 1.0f : 0.0f);
		}
		else
		{
			// しきい値は BasePropertyOverrides ではなくマテリアルのパラメータで持つ
			// (値ごとに静的 permutation が増えるのを避けるため)。
			UMaterialEditingLibrary::SetMaterialInstanceScalarParameterValue(MI, TEXT("AlphaCutoff"),
				Plan.AlphaCutoff);
		}

		MI->PostEditChange();
		SavePackageOfAsset(MI);

		Slots[SlotIndex].MaterialInterface = MI;
		Result.Converted++;
		if (bTranslucent) Result.Translucent++;
		if (bTranslucent && Plan.bPromotedByOrigTexture) Result.Promoted++;
		if (bTranslucent && Plan.bOverlay) Result.Overlay++;
	}

	Result.ExtractedTextures = Textures.NumExtracted();

	// 輪郭線を描く材質があるモデルでだけ、輪郭線マスターを用意する。
	// 実際に描くのは UMmdOutlineComponent (アクター側) なので、ここでは素材を置くだけ。
	if (Result.WithOutline > 0)
	{
		if (EnsureOutlineMaterial(PackagePath) == nullptr)
		{
			UE_LOG(LogMmdPhysics, Warning,
				TEXT("[MmdPhysics] 輪郭線マスターを用意できませんでした。輪郭線は出ません。"));
		}
	}

	Mesh->MarkPackageDirty();
	SavePackageOfAsset(Mesh);

	// ===================================================================
	// 描画順について (移植元の renderQueue 調整に対応する部分)
	//
	// 帯の振り分けをどうマスターの選択へ置き換えたかは PlanMaterial の注記を参照。
	// ここで追加のコードが要らない理由だけ書いておく。
	//
	// 1) 「不透明寄り (AlphaTest帯) が真の半透明より先」
	//    → Masked マスターは不透明パスで描かれ深度を書く。半透明パスは必ずその後なので
	//      構造的に成立する。振り分けるコードは要らない。
	//
	// 2) 「+slotIdx で MMD の材質順を保つ」
	//    → UE の半透明ソートキーは Priority(16) → Distance(32) → MeshIdInPrimitive(16) の順で、
	//      同じスケルタルメッシュのセクションは Priority も Distance も同値になるため、
	//      最下位の MeshIdInPrimitive (= セクション順 = マテリアルスロット順) で決まる。
	//      つまり MMD の材質順がそのまま再現される。これも追加のコードは要らない。
	//      (Engine/Source/Runtime/Renderer/Private/BasePassRendering.cpp
	//       CalculateTranslucentMeshStaticSortKey / Public/MeshPassProcessor.h の
	//       FMeshDrawCommandSortKey::Translucent)
	//
	// 3) TranslucencySortPriority 自体はマテリアルではなく UPrimitiveComponent のプロパティで、
	//    1 コンポーネントに 1 個しか無い。スロット単位では設定できないので、ここからは触らない。
	//    効くのは「このモデル全体 vs シーン内の他の半透明アクター」の順序で、必要なら
	//    レベル側でスケルタルメッシュコンポーネントに設定する (README のトラブルシュート参照)。
	// ===================================================================

	Result.bSuccess = Result.Converted > 0;
	Result.Message = FString::Printf(
		TEXT("マテリアルを変換しました: %d / %d スロット ")
		TEXT("(半透明 %d〈うち昇格 %d・貼り付け材質 %d〉/ 無加工テクスチャ %d / GLB から抽出 %d / マスター: %s%s)"),
		Result.Converted, Result.Total, Result.Translucent, Result.Promoted, Result.Overlay,
		Result.OrigTextureApplied, Result.ExtractedTextures, KMasterName,
		Result.Translucent > 0 ? TEXT(" + M_MmdToonTranslucent") : TEXT(""));
	UE_LOG(LogMmdPhysics, Log, TEXT("[MmdPhysics] %s"), *Result.Message);
	return Result;
}

#undef LOCTEXT_NAMESPACE
