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
		// 真の半透明 (alphaClass=blend) で無加工版がある = IA の透け髪。
		// ★テクスチャを持ち、貼り付け材質でもないので TwoPass 構成になる:
		//   本体は 1 パス目 (Masked, α >= 0.5 の芯 + 深度)、2 パス目は別コンポーネント。
		const FMmdMaterialPlan Promoted = FMmdMaterialConversion::PlanMaterial(
			MakeInfo(TEXT("MASK"), 0.5f, TEXT("blend"), 3, 2));
		TestTrue(TEXT("alphaClass=blend + origTexture は昇格扱い"), Promoted.bPromotedByOrigTexture);
		TestTrue(TEXT("2 パス目が要る"), Promoted.bSoftPass);
		TestFalse(TEXT("本体は 1 パス目なので Masked"), Promoted.bTranslucent);
		TestEqual(TEXT("1 パス目のしきい値は 0.5 (lilToon の _SubpassCutoff)"), Promoted.AlphaCutoff, 0.5f);
		TestTrue(TEXT("無加工テクスチャを使う"), Promoted.bUseOrigTexture);

		// mask 由来の組 (肌・服・メガネ)。見た目はほぼ不透明なので Masked のまま。
		// 不透明パスで深度を書き、半透明より必ず先に描かれる = 移植元の AlphaTest 帯。
		const FMmdMaterialPlan MaskOrig = FMmdMaterialConversion::PlanMaterial(
			MakeInfo(TEXT("MASK"), 0.25f, TEXT("mask"), 3));
		TestFalse(TEXT("alphaClass=mask なら origTexture があっても Masked のまま"), MaskOrig.bTranslucent);
		TestFalse(TEXT("昇格していない"), MaskOrig.bPromotedByOrigTexture);
		TestFalse(TEXT("2 パス目も要らない"), MaskOrig.bSoftPass);
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

		// ★眉は TwoPass にしない。1 パス目は α >= 0.5 を生の色のまま不透明に塗るので、
		//   0.5〜0.7 の薄い墨で描かれた眉はそこで濃さが戻り、黒フチになる (実機で確認)。
		//   MMD は全部を素のアルファでブレンドするので、貼り付け材質はそちらに倣う。
		const FMmdMaterialPlan Brow = FMmdMaterialConversion::PlanMaterial(FaceInfo, MakeStats(0.29f, 0.59f));
		TestTrue(TEXT("貼り付け材質は昇格扱い"), Brow.bPromotedByOrigTexture);
		TestTrue(TEXT("眉は素のアルファでブレンドする"), Brow.bTranslucent);
		TestFalse(TEXT("眉に TwoPass は使わない (黒フチの原因)"), Brow.bSoftPass);
		TestTrue(TEXT("昇格したので無加工版を使う"), Brow.bUseOrigTexture);

		const FMmdMaterialPlan Skin = FMmdMaterialConversion::PlanMaterial(FaceInfo, MakeStats(0.00f, 1.00f));
		TestFalse(TEXT("肌は Masked のまま"), Skin.bTranslucent);
		TestFalse(TEXT("肌には 2 パス目が要らない"), Skin.bSoftPass);
		TestFalse(TEXT("肌には無加工版を使わない"), Skin.bUseOrigTexture);
		TestEqual(TEXT("肌のしきい値は glTF の alphaCutoff のまま"), Skin.AlphaCutoff, 0.1f);

		// 測れなかった場合は昇格させない (誤判定より安全側)。
		const FMmdMaterialPlan Unmeasured =
			FMmdMaterialConversion::PlanMaterial(FaceInfo, MakeStats(0.29f, 0.59f, 0));
		TestFalse(TEXT("標本が足りなければ判定しない"), Unmeasured.bOverlay);
		TestFalse(TEXT("測れなければ 2 パス目も使わない"), Unmeasured.bSoftPass);

		// origTexture が無ければ、いくら半透明でも差し替え先が無いので昇格しない。
		const FMmdMaterialPlan NoOrig = FMmdMaterialConversion::PlanMaterial(
			MakeInfo(TEXT("MASK"), 0.1f, TEXT("mask"), -1, 2), MakeStats(0.29f, 0.59f));
		TestFalse(TEXT("origTexture が無ければ貼り付け判定しない"), NoOrig.bOverlay);

		// 透け髪 (テクスチャあり) は TwoPass。
		const FMmdMaterialPlan BlendOrig = FMmdMaterialConversion::PlanMaterial(
			MakeInfo(TEXT("MASK"), 0.1f, TEXT("blend"), 11, 10));
		TestTrue(TEXT("透け髪は 2 パス目が要る"), BlendOrig.bSoftPass);
		TestTrue(TEXT("透け髪は無加工版を使う"), BlendOrig.bUseOrigTexture);

		// 元から BLEND のものは「昇格」ではない (集計を分けるため)。
		const FMmdMaterialPlan Blend = FMmdMaterialConversion::PlanMaterial(
			MakeInfo(TEXT("BLEND"), 0.0f, TEXT("blend"), 3));
		TestFalse(TEXT("元から BLEND のものは昇格に数えない"), Blend.bPromotedByOrigTexture);
		TestTrue(TEXT("BLEND はアルファブレンドするので無加工テクスチャを使う"), Blend.bUseOrigTexture);

		// ★テクスチャを持たない半透明 (レンズのような単色ガラス) は 1 枚で描く。
		//   移植元も TwoPass にしない。芯と毛先を分ける意味がないため。
		const FMmdMaterialPlan PlainBlend = FMmdMaterialConversion::PlanMaterial(
			MakeInfo(TEXT("BLEND"), 0.0f, TEXT("blend"), -1));
		TestTrue(TEXT("テクスチャの無い BLEND は素の半透明のまま"), PlainBlend.bTranslucent);
		TestFalse(TEXT("テクスチャの無い BLEND に 2 パス目は要らない"), PlainBlend.bSoftPass);
		TestFalse(TEXT("origTexture が無ければ差し替えない"), PlainBlend.bUseOrigTexture);
	}

	// --- TwoPass (移植元 lilToon の TransparentMode=TwoPass 相当) ---
	// ★UE のマテリアルは 1 枚で 2 パスを持てない (Masked と Translucent は排他)。
	//   本体を 1 パス目 (Masked) にし、2 パス目は UMmdSoftPassComponent が描く。
	//   これが無いと、髪は「重なると飽和してグラデーションが潰れる」か
	//   「毛先が硬く切れる」かのどちらかにしかならない。
	{
		// 半透明かつテクスチャあり (透け髪) → TwoPass
		const FMmdMaterialPlan Hair = FMmdMaterialConversion::PlanMaterial(
			MakeInfo(TEXT("MASK"), 0.1f, TEXT("blend"), 11, 10));
		TestTrue(TEXT("テクスチャのある半透明は TwoPass"), Hair.bSubpassOpaque);
		TestTrue(TEXT("2 パス目が要る"), Hair.bSoftPass);
		TestFalse(TEXT("本体は 1 パス目なので Masked"), Hair.bTranslucent);

		// 半透明でテクスチャ無し (レンズのような単色ガラス) → 1 枚で描く
		const FMmdMaterialPlan Lens = FMmdMaterialConversion::PlanMaterial(
			MakeInfo(TEXT("BLEND"), 0.0f, TEXT("opaque"), -1, -1));
		TestTrue(TEXT("レンズは半透明のまま"), Lens.bTranslucent);
		TestFalse(TEXT("テクスチャの無い半透明は TwoPass にしない"), Lens.bSubpassOpaque);
		TestFalse(TEXT("2 パス目も要らない"), Lens.bSoftPass);

		// Masked の材質にはそもそも関係しない
		const FMmdMaterialPlan Skin = FMmdMaterialConversion::PlanMaterial(
			MakeInfo(TEXT("MASK"), 0.1f, TEXT("mask"), 3, 2));
		TestFalse(TEXT("Masked の材質は TwoPass にしない"), Skin.bSubpassOpaque);
		TestFalse(TEXT("2 パス目も要らない"), Skin.bSoftPass);
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
	int32 OverlayCount = 0, OutlineMismatched = 0, OutlineCount = 0;
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

		// --- TwoPass の 2 パス目が要る材質に印が付いているか ---
		// 実際に描くのは UMmdSoftPassComponent で、この印を見て判断する。
		{
			float ActualSoftPass = -1.0f;
			MI->GetScalarParameterValue(FMaterialParameterInfo(TEXT("SoftPass")), ActualSoftPass);
			const float WantSoftPass = Plan.bSoftPass ? 1.0f : 0.0f;
			if (!FMath::IsNearlyEqual(ActualSoftPass, WantSoftPass, 1e-4f))
			{
				SubpassMismatched++;
				AddError(FString::Printf(
					TEXT("スロット '%s': SoftPass が %g (期待 %g)"), *SlotName, ActualSoftPass, WantSoftPass));
			}
			if (Plan.bSoftPass)
			{
				SubpassCount++;
				// 2 パス目を持つ材質の本体は 1 パス目 = Masked でなければならない。
				if (bIsTranslucent)
				{
					SubpassMismatched++;
					AddError(FString::Printf(
						TEXT("スロット '%s': 2 パス目を持つのに本体が Translucent になっている"), *SlotName));
				}
			}
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

		// --- 輪郭線フラグ (PMX flags bit4) が入っているか ---
		{
			float ActualOutline = -1.0f;
			MI->GetScalarParameterValue(FMaterialParameterInfo(TEXT("UseOutline")), ActualOutline);
			const float WantOutline = Info->HasEdge() ? 1.0f : 0.0f;
			if (!FMath::IsNearlyEqual(ActualOutline, WantOutline, 1e-4f))
			{
				OutlineMismatched++;
				AddError(FString::Printf(
					TEXT("スロット '%s': flags=%d (輪郭線 %s) なのに UseOutline=%g"),
					*SlotName, Info->Flags, Info->HasEdge() ? TEXT("あり") : TEXT("なし"), ActualOutline));
			}
			if (Info->HasEdge()) OutlineCount++;
		}

		// --- テクスチャが「色」として取り込まれているか ---
		//
		// ★UE のテクスチャインポータは中身から法線マップかどうかを**推定**する。
		//   MMD の髪テクスチャ (青緑〜紫) は法線マップの基準色 (128,128,255) に近いので
		//   引っかかり、TC_Normalmap (BC5 = 青チャンネル無し) + sRGB=false で取り込まれる。
		//   そのまま色として読むと青が 0 になり、青緑の髪が**黄緑**で描かれる
		//   (Tda式初音ミク・アペンドで実測)。材質が指すテクスチャは全て色なので、
		//   変換が直しているはず。詳しくは MmdMaterialConversion.cpp の EnsureColorTexture。
		//
		// ★見るのは「色として読めるか」(IsColorTexture) であって「TC_Default か」ではない。
		//   近似トゥーンランプ T_MmdToonApproxNN は**意図的に**無圧縮の TC_EditorIcon で
		//   作ってあり (BC 圧縮するとぼかし幅が崩れる。MmdToonRamp.h)、TC_BC7 も色としては
		//   問題無い。TC_Default を要求すると、これら正しい設定を誤りと判定してしまう。
		for (const TCHAR* TexParam : { TEXT("BaseColorTex"), TEXT("ToonTex"), TEXT("SphereTex") })
		{
			UTexture* Tex = nullptr;
			if (!MI->GetTextureParameterValue(FMaterialParameterInfo(TexParam), Tex)) continue;
			UTexture2D* Tex2D = Cast<UTexture2D>(Tex);
			if (Tex2D == nullptr) continue;
			// 既定の白テクスチャ (パラメータ未設定) は対象外。
			if (Tex2D->GetPathName().StartsWith(TEXT("/Engine/"))) continue;

			if (!FMmdMaterialConversion::IsColorTexture(Tex2D))
			{
				AddError(FString::Printf(
					TEXT("スロット '%s' の %s ('%s') が色として取り込まれていない ")
					TEXT("(圧縮 %d / sRGB %d)。色が化けます。"),
					*SlotName, TexParam, *Tex2D->GetName(),
					static_cast<int32>(Tex2D->CompressionSettings), Tex2D->SRGB ? 1 : 0));
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
	TestEqual(TEXT("TwoPass の印が正しく入っている"), SubpassMismatched, 0);
	TestEqual(TEXT("戻り値の 2 パス目の件数が実際と一致する"), R.SoftPass, SubpassCount);
	AddInfo(FString::Printf(TEXT("TwoPass で描く材質 (毛先パスあり): %d 件"), SubpassCount));

	// --- 輪郭線 ---
	TestEqual(TEXT("輪郭線フラグがマテリアルインスタンスに入っている"), OutlineMismatched, 0);
	TestEqual(TEXT("戻り値の輪郭線材質数が実際と一致する"), R.WithOutline, OutlineCount);
	AddInfo(FString::Printf(TEXT("輪郭線を描く材質: %d 件"), OutlineCount));
	if (OutlineCount > 0)
	{
		// 輪郭線マスターが同じフォルダに用意されているか
		// (実際に描くのは MmdOutlineComponent で、これを名前で探す)。
		const FString OutlinePath = PackagePath / TEXT("M_MmdOutline.M_MmdOutline");
		UMaterial* Outline = LoadObject<UMaterial>(nullptr, *OutlinePath, nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (TestNotNull(TEXT("輪郭線マスターが生成されている"), Outline))
		{
			// エッジ色のアルファ (PMX の値。IA は 0.6〜1.0) を効かせるため半透明。
			TestEqual(TEXT("輪郭線マスターは Translucent"), (int32)Outline->BlendMode, (int32)BLEND_Translucent);
			TestTrue(TEXT("輪郭線マスターは両面 (裏面を描くため)"), Outline->TwoSided);
		}
	}

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
