// Copyright (c) 2026 masaka1024. MIT License.

#include "MmdMaterialConversion.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "Factories/MaterialFactoryNew.h"
#include "Factories/MaterialInstanceConstantFactoryNew.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionAppendVector.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionDotProduct.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionOneMinus.h"
#include "Materials/MaterialExpressionSaturate.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionTransform.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionVertexNormalWS.h"
#include "Materials/MaterialInstanceConstant.h"
#include "MmdGlbMaterialReader.h"
#include "MmdPhysicsCoreLog.h"
#include "UObject/SavePackage.h"

#define LOCTEXT_NAMESPACE "MmdMaterialConversion"

using namespace MmdPhysics;

namespace
{
	const TCHAR* KMasterName = TEXT("M_MmdToon");
	const TCHAR* KMasterTranslucentName = TEXT("M_MmdToonTranslucent");

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

	UTexture2D* FindImportedTexture(const FString& ImageName, const FString& PackagePath)
	{
		if (ImageName.IsEmpty()) return nullptr;

		// アセット名にドットは使えないので、候補は必ずサニタイズしたものにする。
		// (Interchange は "眼球４.bmp" を "眼球４_bmp" として取り込む)
		TArray<FString> Candidates;
		Candidates.AddUnique(ImageName.Replace(TEXT("."), TEXT("_")));   // 眼球４_bmp
		Candidates.AddUnique(FPaths::GetBaseFilename(ImageName));        // 眼球４

		// 1) まず同じフォルダ (モデルと一緒に取り込まれたテクスチャ)。
		for (const FString& Name : Candidates)
		{
			const FString ObjPath = PackagePath / Name + TEXT(".") + Name;
			if (UTexture2D* Tex = LoadObject<UTexture2D>(nullptr, *ObjPath, nullptr, LOAD_NoWarn | LOAD_Quiet))
			{
				return Tex;
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
}

UMaterial* FMmdMaterialConversion::EnsureMasterMaterial(const FString& PackagePath, EMmdMasterVariant Variant)
{
	// プラグイン側でグラフを作り替えたときに、古いマスターを掴み続けないようにする。
	// 版はマテリアル内の "MmdToonVersion" スカラーパラメータで持つ。
	// 7.0: Translucent 版マスターを追加し、グラフ生成を共通化 (2026-08-14)。
	static constexpr float KMasterVersion = 7.0f;

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

	// エッジは今回描かないが、後で使えるようにパラメータだけ持たせる。
	auto* EdgeColor = MakeNode<UMaterialExpressionVectorParameter>(Mat, -1300, 900);
	EdgeColor->ParameterName = TEXT("EdgeColor");
	auto* EdgeSize = MakeNode<UMaterialExpressionScalarParameter>(Mat, -1300, 1000);
	EdgeSize->ParameterName = TEXT("EdgeSize");

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

	// --- 合成: Base * Toon * SphereMul + SphereAdd ---
	auto* Lit = MakeNode<UMaterialExpressionMultiply>(Mat, -300, 100);
	Connect(BaseTex, TEXT("RGB"), Lit, TEXT("A"));
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
	// ★アルファの行き先はブレンドモードで変わる。Masked は OpacityMask (しきい値カット)、
	//   Translucent は Opacity (そのまま混ぜる)。繋ぐ先を間違えても接続自体は成功してしまい、
	//   「半透明が全部不透明」「マスクが効かない」という形でしか気付けない。
	const EMaterialProperty AlphaTarget = bTranslucent ? MP_Opacity : MP_OpacityMask;
	if (!UMaterialEditingLibrary::ConnectMaterialProperty(BaseTex, TEXT("A"), AlphaTarget))
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

	// Translucent マスターは alphaMode=BLEND の材質があるモデルでだけ作る
	// (半透明を持たないモデルのフォルダに未使用アセットを増やさない)。
	UMaterial* TranslucentMaster = nullptr;
	if (Set.Materials.ContainsByPredicate([](const MmdMaterialInfo& M) { return IsBlendMaterial(M); }))
	{
		TranslucentMaster = EnsureMasterMaterial(PackagePath, EMmdMasterVariant::Translucent);
		if (TranslucentMaster == nullptr)
		{
			// 致命ではない。Masked へ倒して続行する (従来どおりしきい値で抜ける)。
			UE_LOG(LogMmdPhysics, Warning,
				TEXT("[MmdPhysics] Translucent マスターを用意できませんでした。")
				TEXT("alphaMode=BLEND の材質も Masked で変換します (半透明がしきい値で切り抜かれます)。"));
		}
	}

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

		// alphaMode で親を選ぶ。BLEND だけ Translucent マスターへ繋ぐ。
		const bool bTranslucent = IsBlendMaterial(*Info) && TranslucentMaster != nullptr;
		UMaterial* Master = bTranslucent ? TranslucentMaster : MaskedMaster;

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

		// --- テクスチャ ---
		if (Set.HasTexture(Info->BaseColorTexture))
		{
			if (UTexture2D* T = FindImportedTexture(Set.TextureImageNames[Info->BaseColorTexture], PackagePath))
			{
				UMaterialEditingLibrary::SetMaterialInstanceTextureParameterValue(MI, TEXT("BaseColorTex"), T);
			}
		}

		// トゥーン: 個別テクスチャがあればそれ。共有トゥーン (toon01..toon10) は GLB に入っていないので
		// プロジェクト内をファイル名で探す (Unity 版と同じ制約。ユーザーが別途用意する)。
		bool bHasToon = false;
		if (Set.HasTexture(Info->ToonTexture))
		{
			if (UTexture2D* T = FindImportedTexture(Set.TextureImageNames[Info->ToonTexture], PackagePath))
			{
				UMaterialEditingLibrary::SetMaterialInstanceTextureParameterValue(MI, TEXT("ToonTex"), T);
				bHasToon = true;
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
		}
		UMaterialEditingLibrary::SetMaterialInstanceScalarParameterValue(MI, TEXT("UseToon"), bHasToon ? 1.0f : 0.0f);

		// --- スフィア ---
		float MulWeight = 0.0f, AddWeight = 0.0f;
		if (Info->SphereMode != 0 && Set.HasTexture(Info->SphereTexture))
		{
			if (UTexture2D* T = FindImportedTexture(Set.TextureImageNames[Info->SphereTexture], PackagePath))
			{
				UMaterialEditingLibrary::SetMaterialInstanceTextureParameterValue(MI, TEXT("SphereTex"), T);
				if (Info->SphereMode == 1) MulWeight = 1.0f;       // 乗算
				else if (Info->SphereMode == 2) AddWeight = 1.0f;  // 加算
				// mode 3 (サブテクスチャ) は未対応。加算でも乗算でもないため 0 のまま。
			}
		}
		UMaterialEditingLibrary::SetMaterialInstanceScalarParameterValue(MI, TEXT("SphereMulWeight"), MulWeight);
		UMaterialEditingLibrary::SetMaterialInstanceScalarParameterValue(MI, TEXT("SphereAddWeight"), AddWeight);

		// --- エッジ (今回は描画しないが値は保存しておく) ---
		UMaterialEditingLibrary::SetMaterialInstanceVectorParameterValue(MI, TEXT("EdgeColor"),
			FLinearColor(Info->EdgeColor[0], Info->EdgeColor[1], Info->EdgeColor[2], Info->EdgeColor[3]));
		UMaterialEditingLibrary::SetMaterialInstanceScalarParameterValue(MI, TEXT("EdgeSize"), Info->EdgeSize);

		// --- 描画設定の上書き ---
		MI->BasePropertyOverrides.bOverride_TwoSided = true;
		MI->BasePropertyOverrides.TwoSided = Info->bDoubleSided || Info->IsDoubleSidedFlag();

		// マスク閾値は Masked 側でしか意味を持たない。Translucent ではアルファをそのまま混ぜる。
		MI->BasePropertyOverrides.bOverride_OpacityMaskClipValue = !bTranslucent;
		if (!bTranslucent)
		{
			// alphaMode=OPAQUE はアルファを見ない (glTF の定義)。閾値 0 で実質不透明にする。
			// MASK はテクスチャのアルファでしきい値カット。
			// ※以前はここを extras.mmd の alphaClass=="opaque" で判定していたが、
			//   alphaClass は "blend" / "mask" / 未記載のいずれかで "opaque" は出てこないため
			//   常に 0.333 になっていた。glTF 側の alphaMode で判定する。
			const bool bOpaque = Info->AlphaMode.IsEmpty()
				|| Info->AlphaMode.Equals(TEXT("OPAQUE"), ESearchCase::IgnoreCase);
			MI->BasePropertyOverrides.OpacityMaskClipValue = bOpaque ? 0.0f : 0.333f;
		}

		MI->PostEditChange();
		SavePackageOfAsset(MI);

		Slots[SlotIndex].MaterialInterface = MI;
		Result.Converted++;
		if (bTranslucent) Result.Translucent++;
	}

	Mesh->MarkPackageDirty();
	SavePackageOfAsset(Mesh);

	// ===================================================================
	// 描画順について (移植元の renderQueue 調整に対応する部分)
	//
	// 移植元 MmdPhysicsImporterWindow.cs は半透明材質の renderQueue を手で振っていた
	// (mask 由来の昇格組 → 2452+slot / 真の半透明 → 3000+slot)。UE では次のように対応する。
	//
	// 1) 「不透明寄り (AlphaTest帯) が真の半透明より先」
	//    → Masked マスターは不透明パスで描かれ深度を書く。半透明パスは必ずその後なので
	//      構造的に成立する。振り分けるコードは要らない。
	//    ※そもそも移植元でこの調整が必要だったのは、origTexture を持つモデルで全材質が
	//      Transparent へ昇格してしまい、不透明相当のものまで半透明キューへ落ちたため。
	//      こちらは alphaMode=BLEND のものだけを半透明にするので、その状況にならない。
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
		TEXT("マテリアルを変換しました: %d / %d スロット (うち半透明 %d / マスター: %s%s)"),
		Result.Converted, Result.Total, Result.Translucent, KMasterName,
		Result.Translucent > 0 ? TEXT(" + M_MmdToonTranslucent") : TEXT(""));
	UE_LOG(LogMmdPhysics, Log, TEXT("[MmdPhysics] %s"), *Result.Message);
	return Result;
}

#undef LOCTEXT_NAMESPACE
