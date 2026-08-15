// Copyright (c) 2026 masaka1024. MIT License.
//
// 【2】マテリアル変換の検証。
//   MmdPhysics.Editor.MaterialPlan       変換方針の分岐。データ不要 (常に走る)
//   MmdPhysics.Editor.ConvertMaterials   実モデルへの適用。MMD_PARITY_GLB /
//                                        MMD_CONV_SKELMESH が未設定ならスキップ

#include "Misc/AutomationTest.h"
#include "HAL/PlatformMisc.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "MmdGlbMaterialReader.h"
#include "MmdMaterialConversion.h"

#if WITH_DEV_AUTOMATION_TESTS

// ===========================================================================
// origTexture / alphaClass / alphaCutoff から変換方針が決まるところ。
//
// 「origTexture があるだけで一律に半透明へ落とさない」のが要点。
// 移植元はここを間違えると、共有テクスチャのモデルで全材質が半透明キューへ行き、
// 前髪の向こうのメガネが消える (Tda式ミクV4X で実測) という壊れ方をした。
// ===========================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMmdMaterialPlanTest, "MmdPhysics.Editor.MaterialPlan",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMmdMaterialPlanTest::RunTest(const FString& Parameters)
{
	auto MakeInfo = [](const TCHAR* AlphaMode, float AlphaCutoff, const TCHAR* AlphaClass, int32 OrigTexture,
		int32 BaseColorTexture = -1)
	{
		MmdPhysics::MmdMaterialInfo Info;
		Info.AlphaMode = AlphaMode;
		Info.AlphaCutoff = AlphaCutoff;
		Info.AlphaClass = AlphaClass;
		Info.OrigTexture = OrigTexture;
		Info.BaseColorTexture = BaseColorTexture;
		return Info;
	};

	// --- alphaCutoff (Masked のしきい値) ---
	{
		const FMmdMaterialPlan Opaque = FMmdMaterialConversion::PlanMaterial(
			MakeInfo(TEXT("OPAQUE"), 0.0f, TEXT("opaque"), -1));
		TestFalse(TEXT("OPAQUE は Masked"), Opaque.bTranslucent);
		// glTF では OPAQUE はアルファを見ない。負のしきい値で「見ない」を表す。
		TestTrue(TEXT("OPAQUE はアルファを見ない"), Opaque.AlphaCutoff < 0.0f);

		const FMmdMaterialPlan Mask = FMmdMaterialConversion::PlanMaterial(
			MakeInfo(TEXT("MASK"), 0.25f, TEXT("mask"), -1));
		TestFalse(TEXT("MASK は Masked"), Mask.bTranslucent);
		TestEqual(TEXT("MASK は glTF の alphaCutoff を使う"), Mask.AlphaCutoff, 0.25f);

		const FMmdMaterialPlan MaskDefault = FMmdMaterialConversion::PlanMaterial(
			MakeInfo(TEXT("MASK"), 0.0f, TEXT("mask"), -1));
		TestEqual(TEXT("alphaCutoff 未記載は glTF 既定の 0.5"), MaskDefault.AlphaCutoff, 0.5f);
	}

	// --- origTexture (無加工テクスチャ) ---
	{
		// 真の半透明 (alphaClass=blend) で無加工版がある = IA の眉・透け髪。
		// alphaMode が MASK でも半透明へ昇格させる。
		const FMmdMaterialPlan Promoted = FMmdMaterialConversion::PlanMaterial(
			MakeInfo(TEXT("MASK"), 0.5f, TEXT("blend"), 3));
		TestTrue(TEXT("alphaClass=blend + origTexture は半透明へ昇格する"), Promoted.bTranslucent);
		TestTrue(TEXT("昇格したことが分かる"), Promoted.bPromotedByOrigTexture);
		TestTrue(TEXT("無加工テクスチャを使う"), Promoted.bUseOrigTexture);

		// mask 由来の昇格組 (肌・服・メガネ)。見た目はほぼ不透明なので Masked のまま。
		// 不透明パスで深度を書き、真の半透明より必ず先に描かれる = 移植元の AlphaTest 帯。
		//
		// ★無加工版は縁の画素が下地と未合成なので、硬いアルファテストで描くと
		//   その濃さがフルで出る (IA の眉・目の輪郭に黒フチが出た)。
		//   Masked で無加工版を使うならディザとセットにする。
		const FMmdMaterialPlan MaskOrig = FMmdMaterialConversion::PlanMaterial(
			MakeInfo(TEXT("MASK"), 0.25f, TEXT("mask"), 3));
		TestFalse(TEXT("alphaClass=mask なら origTexture があっても Masked のまま"), MaskOrig.bTranslucent);
		TestFalse(TEXT("昇格していない"), MaskOrig.bPromotedByOrigTexture);
		TestFalse(TEXT("Masked の材質には無加工テクスチャを使わない"), MaskOrig.bUseOrigTexture);
		TestEqual(TEXT("Masked のままなので alphaCutoff が効く"), MaskOrig.AlphaCutoff, 0.25f);

		// alphaClass 未記載 (古い .glb) は mask 扱い = 従来より安全側。
		const FMmdMaterialPlan NoClass = FMmdMaterialConversion::PlanMaterial(
			MakeInfo(TEXT("MASK"), 0.0f, TEXT(""), 3));
		TestFalse(TEXT("alphaClass 未記載は昇格させない"), NoClass.bTranslucent);
		TestFalse(TEXT("alphaClass 未記載なら無加工テクスチャも使わない"), NoClass.bUseOrigTexture);

		// --- 肌に貼り付けた半透明の材質 (眉・まつげ・額の影) ---
		// ★MMD は 1 枚のテクスチャを不透明な肌と半透明の貼り付けで共有する。
		//   UV 領域を測らないと区別できず、区別しないと眉が硬い黒線になる。
		//   実測 (IA): 肌 mat1 0% / mat3 1% に対し、眉 mat4 43% / 額の影 顔3 29%。
		auto MakeStats = [](float FracSemi, float MeanAlpha, int32 Samples = 500)
		{
			FMmdUvAlphaStats S;
			S.Samples = Samples;
			S.FracSemi = FracSemi;
			S.FracOpaque = 1.0f - FracSemi;
			S.MeanAlpha = MeanAlpha;
			return S;
		};

		const MmdPhysics::MmdMaterialInfo FaceInfo = MakeInfo(TEXT("MASK"), 0.1f, TEXT("mask"), 3, 2);
		auto IsOverlay = [&](float FracSemi, float MeanAlpha)
		{
			return FMmdMaterialConversion::PlanMaterial(FaceInfo, MakeStats(FracSemi, MeanAlpha)).bOverlay;
		};

		// ★実測値をそのまま表にしてある。しきい値を動かすとどれかが壊れる。
		//   モデル 材質        半透明率 平均α  期待
		TestTrue (TEXT("IA  mat4 (眉) は貼り付け"),            IsOverlay(0.29f, 0.59f));
		TestTrue (TEXT("IA  顔3 (額の影) は貼り付け"),          IsOverlay(0.21f, 0.60f));
		TestFalse(TEXT("IA  mat1 (肌) は貼り付けでない"),       IsOverlay(0.00f, 1.00f));
		TestFalse(TEXT("IA  mat3 (肌) は貼り付けでない"),       IsOverlay(0.01f, 1.00f));
		TestFalse(TEXT("V4X body_cloth (服) は貼り付けでない"), IsOverlay(0.12f, 0.99f));
		TestFalse(TEXT("V4X face01 (顔) は貼り付けでない"),     IsOverlay(0.11f, 0.90f));
		TestFalse(TEXT("V4X megane (フレーム) は貼り付けでない"), IsOverlay(0.46f, 0.86f));
		TestTrue (TEXT("V4X hairshadow (髪の影) は貼り付け"),   IsOverlay(0.65f, 0.22f));
		TestTrue (TEXT("V4X cheek (チーク) は貼り付け"),        IsOverlay(0.45f, 0.08f));

		const FMmdMaterialPlan Brow = FMmdMaterialConversion::PlanMaterial(FaceInfo, MakeStats(0.29f, 0.59f));
		TestTrue(TEXT("貼り付け材質は半透明へ昇格する"), Brow.bTranslucent);
		TestTrue(TEXT("昇格したので無加工版を使う"), Brow.bUseOrigTexture);

		const FMmdMaterialPlan Skin = FMmdMaterialConversion::PlanMaterial(FaceInfo, MakeStats(0.00f, 1.00f));
		TestFalse(TEXT("肌は Masked のまま"), Skin.bTranslucent);
		TestFalse(TEXT("肌には無加工版を使わない"), Skin.bUseOrigTexture);

		// 測れなかった場合は昇格させない (誤判定より安全側)。
		const FMmdMaterialPlan Unmeasured =
			FMmdMaterialConversion::PlanMaterial(FaceInfo, MakeStats(0.29f, 0.59f, 0));
		TestFalse(TEXT("標本が足りなければ判定しない"), Unmeasured.bOverlay);
		TestFalse(TEXT("測れなければ昇格させない"), Unmeasured.bTranslucent);

		// origTexture が無ければ、いくら半透明でも差し替え先が無いので昇格しない。
		const FMmdMaterialPlan NoOrig = FMmdMaterialConversion::PlanMaterial(
			MakeInfo(TEXT("MASK"), 0.1f, TEXT("mask"), -1, 2), MakeStats(0.29f, 0.59f));
		TestFalse(TEXT("origTexture が無ければ貼り付け判定しない"), NoOrig.bOverlay);

		// 半透明側はブレンドできるので無加工版が使える。
		const FMmdMaterialPlan BlendOrig = FMmdMaterialConversion::PlanMaterial(
			MakeInfo(TEXT("MASK"), 0.1f, TEXT("blend"), 11, 10));
		TestTrue(TEXT("昇格した材質は半透明"), BlendOrig.bTranslucent);
		TestTrue(TEXT("半透明側は無加工版を使う"), BlendOrig.bUseOrigTexture);

		// 元から BLEND のものは「昇格」ではない (集計を分けるため)。
		const FMmdMaterialPlan Blend = FMmdMaterialConversion::PlanMaterial(
			MakeInfo(TEXT("BLEND"), 0.0f, TEXT("blend"), 3));
		TestTrue(TEXT("BLEND は半透明"), Blend.bTranslucent);
		TestFalse(TEXT("元から BLEND のものは昇格に数えない"), Blend.bPromotedByOrigTexture);
		TestTrue(TEXT("BLEND はアルファブレンドするので無加工テクスチャを使う"), Blend.bUseOrigTexture);

		const FMmdMaterialPlan PlainBlend = FMmdMaterialConversion::PlanMaterial(
			MakeInfo(TEXT("BLEND"), 0.0f, TEXT("blend"), -1));
		TestTrue(TEXT("origTexture が無くても BLEND は半透明"), PlainBlend.bTranslucent);
		TestFalse(TEXT("origTexture が無ければ差し替えない"), PlainBlend.bUseOrigTexture);
	}

	// --- 不透明サブパス (移植元 lilToon の TwoPass 相当) ---
	// ★これが無いと、α 0.75〜1.0 の帯が広い髪テクスチャが一様に透けて
	//   「前髪ごしに眉が透けすぎる」形で移植元と食い違う。
	{
		// 半透明かつテクスチャあり (透け髪) → サブパスで α>=0.5 を不透明に描く
		const FMmdMaterialPlan Hair = FMmdMaterialConversion::PlanMaterial(
			MakeInfo(TEXT("MASK"), 0.1f, TEXT("blend"), 11, 10));
		TestTrue(TEXT("透け髪は半透明"), Hair.bTranslucent);
		TestTrue(TEXT("テクスチャのある半透明は不透明サブパスを使う"), Hair.bSubpassOpaque);

		// 半透明でテクスチャ無し (レンズのような単色ガラス) → 素のブレンド
		const FMmdMaterialPlan Lens = FMmdMaterialConversion::PlanMaterial(
			MakeInfo(TEXT("BLEND"), 0.0f, TEXT("opaque"), -1, -1));
		TestTrue(TEXT("レンズは半透明"), Lens.bTranslucent);
		TestFalse(TEXT("テクスチャの無い半透明はサブパスを使わない"), Lens.bSubpassOpaque);

		// Masked の材質にはそもそも関係しない
		const FMmdMaterialPlan Skin = FMmdMaterialConversion::PlanMaterial(
			MakeInfo(TEXT("MASK"), 0.1f, TEXT("mask"), 3, 2));
		TestFalse(TEXT("Masked の材質はサブパスを使わない"), Skin.bSubpassOpaque);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMmdMaterialConversionTest, "MmdPhysics.Editor.ConvertMaterials",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMmdMaterialConversionTest::RunTest(const FString& Parameters)
{
	const FString GlbPath = FPlatformMisc::GetEnvironmentVariable(TEXT("MMD_PARITY_GLB"));
	const FString MeshPath = FPlatformMisc::GetEnvironmentVariable(TEXT("MMD_CONV_SKELMESH"));
	if (GlbPath.IsEmpty() || MeshPath.IsEmpty())
	{
		AddInfo(TEXT("MMD_PARITY_GLB / MMD_CONV_SKELMESH が未設定のためスキップ。"));
		return true;
	}

	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *MeshPath);
	if (Mesh == nullptr)
	{
		AddError(FString::Printf(TEXT("スケルタルメッシュを読めない: %s"), *MeshPath));
		return false;
	}

	const FMmdMaterialResult R = FMmdMaterialConversion::ConvertMaterials(Mesh, GlbPath);
	AddInfo(R.Message);

	if (!TestTrue(TEXT("マテリアル変換が成功する"), R.bSuccess)) return false;
	TestEqual(TEXT("全スロットが変換された"), R.Converted, R.Total);

	// 実際に MI が割り当たっていて、パラメータが入っているかを見る。
	int32 WithToon = 0, WithSphere = 0, MiCount = 0;
	for (const FSkeletalMaterial& Slot : Mesh->GetMaterials())
	{
		UMaterialInstanceConstant* MI = Cast<UMaterialInstanceConstant>(Slot.MaterialInterface);
		if (MI == nullptr) continue;
		MiCount++;

		float UseToon = 0.0f, MulW = 0.0f, AddW = 0.0f;
		MI->GetScalarParameterValue(FMaterialParameterInfo(TEXT("UseToon")), UseToon);
		MI->GetScalarParameterValue(FMaterialParameterInfo(TEXT("SphereMulWeight")), MulW);
		MI->GetScalarParameterValue(FMaterialParameterInfo(TEXT("SphereAddWeight")), AddW);
		if (UseToon > 0.5f) WithToon++;
		if (MulW > 0.5f || AddW > 0.5f) WithSphere++;
	}

	AddInfo(FString::Printf(TEXT("マテリアルインスタンス %d / トゥーン有効 %d / スフィア有効 %d"),
		MiCount, WithToon, WithSphere));

	TestEqual(TEXT("全スロットにマテリアルインスタンスが割り当たっている"), MiCount, Mesh->GetMaterials().Num());

	// このモデルはスフィアが 14 材質にあるので、少なくとも数件は有効になるはず。
	// (トゥーンは共有トゥーン画像が無いと 0 件になり得るので、ここでは必須にしない)
	TestTrue(FString::Printf(TEXT("スフィアマップが適用された材質がある (%d)"), WithSphere), WithSphere > 0);

	if (WithToon == 0)
	{
		AddWarning(TEXT("トゥーンが 1 件も有効になっていません。")
			TEXT("共有トゥーン (toon01..toon10) はモデルに同梱されないため、")
			TEXT("プロジェクトへ別途取り込む必要があります。"));
	}

	// --- alphaMode=BLEND が Translucent マスターへ繋がっているか ---
	// .glb 側の alphaMode を正として、スロット名で突き合わせる。
	MmdPhysics::MmdMaterialSet Set;
	TArray<FString> Warnings;
	if (!MmdPhysics::GlbMaterialReader::LoadFile(GlbPath, Set, Warnings))
	{
		AddError(TEXT(".glb からマテリアル情報を読めない (alphaMode の照合をスキップ)。"));
		return false;
	}

	int32 ExpectedBlend = 0, ActualTranslucent = 0, Mismatched = 0;
	int32 ExpectedPromoted = 0, WithOrigTexture = 0, OrigTextureApplied = 0, CutoffMismatched = 0;
	int32 ColorMismatched = 0, TexturelessCount = 0, SubpassMismatched = 0, SubpassCount = 0;
	// 変換と同じ場所からテクスチャを引くために、メッシュのフォルダを求めておく。
	const FString PackagePath = FPackageName::GetLongPackagePath(Mesh->GetOutermost()->GetName());
	int32 OverlayCount = 0;
	const TArray<FSkeletalMaterial>& CheckSlots = Mesh->GetMaterials();
	for (int32 i = 0; i < CheckSlots.Num(); i++)
	{
		const FString SlotName = CheckSlots[i].MaterialSlotName.ToString();
		const MmdPhysics::MmdMaterialInfo* Info = Set.Materials.FindByPredicate(
			[&SlotName](const MmdPhysics::MmdMaterialInfo& M) { return M.Name == SlotName; });
		if (Info == nullptr && Set.Materials.IsValidIndex(i)) Info = &Set.Materials[i];
		if (Info == nullptr) continue;

		UMaterialInstanceConstant* MI = Cast<UMaterialInstanceConstant>(CheckSlots[i].MaterialInterface);
		if (MI == nullptr) continue;

		const UMaterial* Parent = MI->GetMaterial();

		// 変換側と同じ材料で方針を組み立て直す (UV 領域の測定込み)。
		FMmdUvAlphaStats UvStats;
		if (Set.HasTexture(Info->OrigTexture))
		{
			UTexture2D* OrigTex2D = FMmdMaterialConversion::FindImportedTextureByImageName(
				Set.TextureImageNames[Info->OrigTexture], PackagePath);
			if (OrigTex2D != nullptr)
			{
				FMmdMaterialConversion::MeasureUvAlpha(Mesh, i, OrigTex2D, UvStats);
			}
		}
		const FMmdMaterialPlan Plan = FMmdMaterialConversion::PlanMaterial(*Info, UvStats);
		if (Plan.bOverlay) OverlayCount++;
		// GetBlendMode() はインスタンスの BasePropertyOverrides まで解決した実効値を返す。
		const bool bIsTranslucent = (MI->GetBlendMode() == BLEND_Translucent);

		if (Plan.bTranslucent) ExpectedBlend++;
		if (Plan.bPromotedByOrigTexture) ExpectedPromoted++;
		if (bIsTranslucent) ActualTranslucent++;
		if (Plan.bTranslucent != bIsTranslucent)
		{
			Mismatched++;
			AddError(FString::Printf(
				TEXT("スロット '%s': alphaMode=%s alphaClass=%s origTexture=%d なのに親は %s (Translucent=%s)"),
				*SlotName, *Info->AlphaMode, *Info->AlphaClass, Info->OrigTexture,
				Parent ? *Parent->GetName() : TEXT("null"),
				bIsTranslucent ? TEXT("true") : TEXT("false")));
		}

		// --- 拡散色 (baseColorFactor) が入っているか ---
		// ★テクスチャを持たない材質 (IA のレンズ・銀・髪止め・ベルト) はここにしか色が無い。
		//   落とすと既定の白テクスチャのまま真っ白に出るが、変換自体は成功するので
		//   件数のテストでは捕まらない。値そのものを突き合わせる。
		{
			FLinearColor ActualColor = FLinearColor::White;
			MI->GetVectorParameterValue(FMaterialParameterInfo(TEXT("BaseColor")), ActualColor);
			const FLinearColor WantColor(Info->BaseColorFactor[0], Info->BaseColorFactor[1],
				Info->BaseColorFactor[2], Info->BaseColorFactor[3]);
			if (!ActualColor.Equals(WantColor, 1e-4f))
			{
				ColorMismatched++;
				AddError(FString::Printf(
					TEXT("スロット '%s': 拡散色が %s (期待 %s)"),
					*SlotName, *ActualColor.ToString(), *WantColor.ToString()));
			}
			if (Info->BaseColorTexture < 0) TexturelessCount++;
		}

		// --- 不透明サブパス (lilToon TwoPass 相当) の重みが入っているか ---
		if (bIsTranslucent)
		{
			float ActualWeight = -1.0f;
			MI->GetScalarParameterValue(FMaterialParameterInfo(TEXT("SubpassWeight")), ActualWeight);
			const float WantWeight = Plan.bSubpassOpaque ? 1.0f : 0.0f;
			if (!FMath::IsNearlyEqual(ActualWeight, WantWeight, 1e-4f))
			{
				SubpassMismatched++;
				AddError(FString::Printf(
					TEXT("スロット '%s': SubpassWeight が %g (期待 %g)"), *SlotName, ActualWeight, WantWeight));
			}
			if (Plan.bSubpassOpaque) SubpassCount++;
		}

		// --- alphaCutoff がマスクのしきい値パラメータに入っているか (Masked のときだけ) ---
		if (!bIsTranslucent)
		{
			float ActualCutoff = 0.0f;
			MI->GetScalarParameterValue(FMaterialParameterInfo(TEXT("AlphaCutoff")), ActualCutoff);
			if (!FMath::IsNearlyEqual(ActualCutoff, Plan.AlphaCutoff, 1e-4f))
			{
				CutoffMismatched++;
				AddError(FString::Printf(
					TEXT("スロット '%s': alphaMode=%s alphaCutoff=%g なのにしきい値が %g (期待 %g)"),
					*SlotName, *Info->AlphaMode, Info->AlphaCutoff, ActualCutoff, Plan.AlphaCutoff));
			}
		}

		// --- origTexture: BaseColorTex が無加工版に差し替わっているか ---
		if (Plan.bUseOrigTexture)
		{
			WithOrigTexture++;
			UTexture* BaseTex = nullptr;
			MI->GetTextureParameterValue(FMaterialParameterInfo(TEXT("BaseColorTex")), BaseTex);

			if (BaseTex != nullptr && Set.HasTexture(Info->OrigTexture))
			{
				// アセット名は画像名のドットを '_' に置き換えたもの ("眼球４.bmp" → "眼球４_bmp")。
				const FString WantName = Set.TextureImageNames[Info->OrigTexture].Replace(TEXT("."), TEXT("_"));
				if (BaseTex->GetName().Equals(WantName, ESearchCase::IgnoreCase))
				{
					OrigTextureApplied++;
				}
				else
				{
					// 抽出に失敗するとプリベイク版のまま続行する仕様なので、警告に留める。
					AddWarning(FString::Printf(
						TEXT("スロット '%s': BaseColorTex が '%s' で、無加工版 '%s' に差し替わっていません。"),
						*SlotName, *BaseTex->GetName(), *WantName));
				}
			}
		}
	}

	AddInfo(FString::Printf(
		TEXT("半透明にすべき %d 件 (うち origTexture 昇格 %d) / 実際に Translucent %d 件 / ")
		TEXT("origTexture 持ち %d 件 (差し替え成功 %d)"),
		ExpectedBlend, ExpectedPromoted, ActualTranslucent, WithOrigTexture, OrigTextureApplied));
	TestEqual(TEXT("半透明にすべき材質だけが Translucent マスターへ繋がっている"), Mismatched, 0);
	TestEqual(TEXT("戻り値の Translucent 件数が実際と一致する"), R.Translucent, ActualTranslucent);
	TestEqual(TEXT("戻り値の昇格件数が実際と一致する"), R.Promoted, ExpectedPromoted);
	TestEqual(TEXT("戻り値の貼り付け材質の件数が実際と一致する"), R.Overlay, OverlayCount);
	AddInfo(FString::Printf(TEXT("肌に貼り付けた半透明の材質: %d 件"), OverlayCount));
	TestEqual(TEXT("マスク閾値が glTF の alphaCutoff と一致する"), CutoffMismatched, 0);
	TestEqual(TEXT("拡散色 (baseColorFactor) がマテリアルインスタンスに入っている"), ColorMismatched, 0);
	AddInfo(FString::Printf(TEXT("ベーステクスチャを持たない材質: %d 件 (拡散色だけで色が決まる)"), TexturelessCount));
	TestEqual(TEXT("不透明サブパス (TwoPass 相当) の重みが正しい"), SubpassMismatched, 0);
	AddInfo(FString::Printf(TEXT("不透明サブパスを使う半透明材質: %d 件"), SubpassCount));

	if (WithOrigTexture > 0)
	{
		// 個々の差し替えは失敗してもプリベイク版で続行する (上で警告にしている) が、
		// 1 件も差し替わらないなら経路そのものが動いていない。
		TestTrue(FString::Printf(TEXT("無加工テクスチャへ差し替えられた材質がある (%d / %d)"),
			R.OrigTextureApplied, WithOrigTexture), R.OrigTextureApplied > 0);
		TestTrue(FString::Printf(TEXT("差し替え先が無加工版の名前と一致している (%d 件)"), OrigTextureApplied),
			OrigTextureApplied > 0);
		// 2 回目以降は前回抽出したアセットが名前で見つかるので、抽出数は 0 になり得る。
		// (だから件数そのものは検証条件にしない)
		AddInfo(FString::Printf(TEXT("今回 GLB から取り出したテクスチャ: %d 枚"), R.ExtractedTextures));
	}

	if (ExpectedBlend == 0)
	{
		AddWarning(TEXT("このモデルには半透明として描く材質がありません。")
			TEXT("半透明の経路は実質未検証です。"));
	}
	if (WithOrigTexture == 0)
	{
		AddWarning(TEXT("このモデルには origTexture を持つ材質がありません。")
			TEXT("GLB からのテクスチャ抽出と昇格の経路は実質未検証です。"));
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
