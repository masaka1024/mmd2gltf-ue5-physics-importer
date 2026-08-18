// Copyright (c) 2026 masaka1024. MIT License.
//
// 【3】アクター生成の検証。MMD_CONV_SKELMESH が未設定ならスキップ。
//
// 見ているのは「組み上がった Blueprint に本体と輪郭線が入っているか」。
// 輪郭線を描く材質があるかどうかは【2】の結果 (マテリアルの UseOutline) から決まるので、
// このテストは【2】まで済んだプロジェクトで走らせる前提。

#include "Misc/AutomationTest.h"
#include "HAL/PlatformMisc.h"
#include "Animation/AnimCurveMetadata.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "AnimationBlueprintLibrary.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Blueprint.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInterface.h"
#include "MmdActorBuilder.h"
#include "MmdMorphAnimation.h"
#include "MmdOutlineComponent.h"
#include "MmdSoftPassComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMmdActorBuilderTest, "MmdPhysics.Editor.BuildActor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMmdActorBuilderTest::RunTest(const FString& Parameters)
{
	const FString MeshPath = FPlatformMisc::GetEnvironmentVariable(TEXT("MMD_CONV_SKELMESH"));
	if (MeshPath.IsEmpty())
	{
		AddInfo(TEXT("MMD_CONV_SKELMESH が未設定のためスキップ。"));
		return true;
	}

	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *MeshPath);
	if (Mesh == nullptr)
	{
		AddError(FString::Printf(TEXT("スケルタルメッシュを読めない: %s"), *MeshPath));
		return false;
	}

	// .glb があれば渡す (表情モーフのカーブ補完まで検証する)。無くてもボーンまでは見る。
	const FString GlbPath = FPlatformMisc::GetEnvironmentVariable(TEXT("MMD_PARITY_GLB"));

	const FMmdActorResult R = FMmdActorBuilder::BuildActor(Mesh, GlbPath);
	AddInfo(R.Message);
	if (!TestTrue(TEXT("アクターを生成できる"), R.bSuccess)) return false;
	if (!TestNotNull(TEXT("Blueprint が返る"), R.Blueprint)) return false;

	USimpleConstructionScript* SCS = R.Blueprint->SimpleConstructionScript;
	if (!TestNotNull(TEXT("コンポーネント構成がある"), SCS)) return false;

	// --- 本体 ---
	USkeletalMeshComponent* MeshTemplate = nullptr;
	UMmdOutlineComponent* OutlineTemplate = nullptr;
	UMmdSoftPassComponent* SoftPassTemplate = nullptr;
	for (USCS_Node* Node : SCS->GetAllNodes())
	{
		if (Node == nullptr) continue;
		if (auto* Outline = Cast<UMmdOutlineComponent>(Node->ComponentTemplate))
		{
			OutlineTemplate = Outline;
		}
		else if (auto* SoftPass = Cast<UMmdSoftPassComponent>(Node->ComponentTemplate))
		{
			SoftPassTemplate = SoftPass;
		}
		// ★追従コンポーネントもスケルタルメッシュなので、先に弾いてから本体を拾う。
		else if (auto* SkelMesh = Cast<USkeletalMeshComponent>(Node->ComponentTemplate))
		{
			MeshTemplate = SkelMesh;
		}
	}

	if (TestNotNull(TEXT("スケルタルメッシュコンポーネントがある"), MeshTemplate))
	{
		TestEqual(TEXT("対象のメッシュが割り当たっている"),
			MeshTemplate->GetSkeletalMeshAsset(), Mesh);

		// --- アニメーション (VMD 由来のモーション) ---
		// ★これを繋がないと、置いたアクターが参照ポーズのまま立っているだけになる。
		//   モーション入りの .glb かどうかで期待値が変わるので、探索結果を正として突き合わせる。
		int32 Candidates = 0;
		UAnimSequence* Expected = FMmdActorBuilder::FindAnimationFor(Mesh, Candidates);
		AddInfo(FString::Printf(TEXT("同じスケルトンで再生できる AnimSequence: %d 本"), Candidates));
		TestEqual(TEXT("戻り値の候補数が探索結果と一致する"), R.AnimationCandidates, Candidates);
		TestEqual(TEXT("戻り値のアニメーションが探索結果と一致する"), R.Animation, Expected);

		if (Expected != nullptr)
		{
			TestEqual(TEXT("アニメーションが割り当たっている"),
				Cast<UAnimSequence>(MeshTemplate->AnimationData.AnimToPlay), Expected);
			// ★Post-Process AnimBP (物理) は再生モードに関わらず後段で走るので、
			//   単発再生にしても物理と共存する。
			TestEqual(TEXT("単発再生モードになっている"),
				(int32)MeshTemplate->GetAnimationMode(), (int32)EAnimationMode::AnimationSingleNode);
			TestTrue(TEXT("再生する設定になっている"), MeshTemplate->AnimationData.bSavedPlaying != 0);
			TestTrue(TEXT("ループする設定になっている"), MeshTemplate->AnimationData.bSavedLooping != 0);

			// --- 表情モーフのアニメーション ---
			// ★UE 5.5 の Interchange は glTF の weights チャンネルを取りこぼすので、
			//   プラグインが .glb から直接読んでカーブを足している。
			//   これが効かないと「体は踊るのに顔が動かない」という壊れ方をする。
			if (!GlbPath.IsEmpty())
			{
				TArray<FName> Curves;
				UAnimationBlueprintLibrary::GetAnimationCurveNames(
					Expected, ERawCurveTrackTypes::RCT_Float, Curves);
				AddInfo(FString::Printf(
					TEXT("アニメーションのカーブ: %d 本 (今回追加 %d 本 / 化けたカーブを消した %d 本)"),
					Curves.Num(), R.MorphCurvesAdded, R.MorphCurvesRemoved));

				// カーブ名はメッシュのモーフターゲット名と一致していなければ何も動かない。
				// ★Interchange が weights を取り込めた .glb では、記号モーフのカーブが
				//   `_` `ω_` のような化けた名前で入っていることがある。プラグインが
				//   それを消しているので、消し損ねるとここで捕まる
				//   (詳しくは MmdMorphAnimation.h の注記)。
				int32 Unmatched = 0;
				for (const FName& Name : Curves)
				{
					if (Mesh->FindMorphTarget(Name) == nullptr) Unmatched++;
				}
				TestEqual(TEXT("カーブ名がすべてモーフターゲットに対応している"), Unmatched, 0);

				// ★名前が合っているだけでは動かない。スケルトンの curve metadata に
				//   「モーフを動かすカーブ」と登録されていないと、UE はカーブを読んでも
				//   モーフへ繋がない (FBoneContainer がここから MorphTarget フラグを作る)。
				//   登録はスケルトン側にあるので、保存されていなければ次にエディタを
				//   開いた時点で消える = 体は踊るのに顔だけ動かない状態になる。
				if (USkeleton* Skeleton = Expected->GetSkeleton())
				{
					int32 Unregistered = 0;
					for (const FName& Name : Curves)
					{
						const FCurveMetaData* Meta = Skeleton->GetCurveMetaData(Name);
						if (Meta == nullptr || !Meta->Type.bMorphtarget) Unregistered++;
					}
					TestEqual(TEXT("カーブがスケルトンにモーフ用として登録されている"), Unregistered, 0);
					TestFalse(TEXT("スケルトンが未保存のまま残っていない"),
						Skeleton->GetOutermost()->IsDirty());
				}

				if (Curves.Num() > 0)
				{
					// 値が入っているか (全部 0 なら足した意味がない)。
					float MaxSeen = 0.0f;
					for (const FName& Name : Curves)
					{
						// モーションのどこかで開くはずなので、数点だけ見る。
						for (int32 i = 0; i <= 20; i++)
						{
							const float Time = Expected->GetPlayLength() * (i / 20.0f);
							MaxSeen = FMath::Max(MaxSeen, Expected->EvaluateCurveData(Name, Time));
						}
					}
					TestTrue(FString::Printf(TEXT("モーフのカーブに 0 でない値がある (最大 %.3f)"), MaxSeen),
						MaxSeen > 0.0f);
				}
				else
				{
					AddWarning(TEXT("アニメーションにモーフのカーブが 1 本もありません。")
						TEXT("モーフキーを持たないモーションなら正常です。"));
				}

				// 2 回目に二重で足さないこと (既存カーブは触らない)。
				const FMmdActorResult Twice = FMmdActorBuilder::BuildActor(Mesh, GlbPath);
				TArray<FName> CurvesAfter;
				UAnimationBlueprintLibrary::GetAnimationCurveNames(
					Expected, ERawCurveTrackTypes::RCT_Float, CurvesAfter);
				TestEqual(TEXT("作り直してもカーブが増えない"), CurvesAfter.Num(), Curves.Num());
				TestEqual(TEXT("2 回目は 1 本も追加しない"), Twice.MorphCurvesAdded, 0);
				// ★1 回目で消しきれていれば 2 回目は消す物が無い。
				//   (これはメモリ上の状態しか見ていない。保存されたかは下の dirty で見る)
				TestEqual(TEXT("2 回目は 1 本も消さない"), Twice.MorphCurvesRemoved, 0);

				// ★書き換えたなら保存されていること。カーブを消しただけの回で
				//   保存を飛ばすと、エディタを開き直した時点で化けたカーブが戻る。
				if (R.MorphCurvesAdded > 0 || R.MorphCurvesRemoved > 0)
				{
					TestFalse(TEXT("アニメーションが未保存のまま残っていない"),
						Expected->GetOutermost()->IsDirty());
				}

				// --- 化けた既存カーブの掃除 ---
				// ★手元のデータではこの経路を踏めない。報告があったのは別環境の
				//   `/Game/TdaFresh` で、そこでは Interchange が作ったカーブのうち
				//   3 本が `_` `ω_` `恐ろしい子_` に化けていた。手元の .glb は
				//   同じ記号モーフを持つのに化けたカーブが入っていない。
				//   そこで**化けた姿のカーブを 1 本その場で作って**、消えることを見る。
				//   (作らずに済ませると、消す処理が動くかどうかを一度も確かめられない)
				//
				// どの名前が化けるかは .glb 次第なので、直接呼んで教えてもらう
				// (BuildActor の戻り値には数しか乗っていない)。
				// 直前に BuildActor が同じことをしたばかりなので、この呼び出しでは何も変わらない。
				const FMmdMorphAnimResult Probe =
					FMmdMorphAnimation::ApplyMorphCurves(Mesh, Expected, GlbPath);
				if (Probe.UnsafeNames.Num() > 0)
				{
					const FName Mangled(*FMmdMorphAnimation::MakeRigSafeName(Probe.UnsafeNames[0]));
					if (Mesh->FindMorphTarget(Mangled) != nullptr)
					{
						// 化けた先に実在のモーフがあるモデル。消してはいけない側なので試さない。
						AddInfo(FString::Printf(
							TEXT("'%s' は実在のモーフなので掃除の確認は飛ばします。"), *Mangled.ToString()));
					}
					else
					{
						{
							// 1 本でも囲まないとアニメーション全体の再圧縮が走る。
							IAnimationDataController::FScopedBracket Bracket(
								Expected->GetController(),
								FText::FromString(TEXT("化けたカーブの掃除を確かめる")),
								/*bShouldTransact=*/false);
							UAnimationBlueprintLibrary::AddCurve(Expected, Mangled,
								ERawCurveTrackTypes::RCT_Float, /*bMetaDataCurve=*/false);
						}

						const FMmdActorResult Cleanup = FMmdActorBuilder::BuildActor(Mesh, GlbPath);
						TestEqual(TEXT("化けたカーブを 1 本消す"), Cleanup.MorphCurvesRemoved, 1);

						TArray<FName> CurvesCleaned;
						UAnimationBlueprintLibrary::GetAnimationCurveNames(
							Expected, ERawCurveTrackTypes::RCT_Float, CurvesCleaned);
						TestFalse(FString::Printf(TEXT("'%s' が消えている"), *Mangled.ToString()),
							CurvesCleaned.Contains(Mangled));
						// ★実在のモーフのカーブを巻き添えにしていないこと。
						//   ここが減っていると「顔が動かなくなる」直し方をしている。
						TestEqual(TEXT("他のカーブは減っていない"), CurvesCleaned.Num(), Curves.Num());

						if (CurvesCleaned.Contains(Mangled))
						{
							// 消せなかったときに、テストが作ったカーブを残さない。
							UAnimationBlueprintLibrary::RemoveCurve(Expected, Mangled);
							AddError(TEXT("掃除できなかったため、テストが作ったカーブを取り除きました。"));
						}
					}
				}
			}
		}
		else
		{
			TestNull(TEXT("見つからなければ割り当てない"),
				(UObject*)MeshTemplate->AnimationData.AnimToPlay);
			AddWarning(TEXT("このスケルトンで再生できる AnimSequence がありません。")
				TEXT("モデルだけの .glb (--vmd 無し) なら正常です。"));
		}
	}

	// --- 輪郭線 ---
	// 付くかどうかは材質側の UseOutline 次第。戻り値と実物が一致していることを見る。
	int32 WithOutline = 0;
	for (const FSkeletalMaterial& Slot : Mesh->GetMaterials())
	{
		if (Slot.MaterialInterface == nullptr) continue;
		float UseOutline = 0.0f;
		Slot.MaterialInterface->GetScalarParameterValue(
			FMaterialParameterInfo(TEXT("UseOutline")), UseOutline);
		if (UseOutline > 0.5f) WithOutline++;
	}
	AddInfo(FString::Printf(TEXT("輪郭線を描く材質: %d 件"), WithOutline));

	// --- TwoPass の 2 パス目 ---
	int32 WithSoftPass = 0;
	for (const FSkeletalMaterial& Slot : Mesh->GetMaterials())
	{
		if (Slot.MaterialInterface == nullptr) continue;
		float SoftPass = 0.0f;
		Slot.MaterialInterface->GetScalarParameterValue(
			FMaterialParameterInfo(TEXT("SoftPass")), SoftPass);
		if (SoftPass > 0.5f) WithSoftPass++;
	}
	AddInfo(FString::Printf(TEXT("2 パス目が要る材質: %d 件"), WithSoftPass));
	TestEqual(TEXT("毛先パスの有無が材質の状態と一致する"), R.bHasSoftPass, WithSoftPass > 0);
	if (WithSoftPass > 0)
	{
		TestNotNull(TEXT("毛先パスのコンポーネントが入っている"), SoftPassTemplate);
	}

	TestEqual(TEXT("輪郭線の有無が材質の状態と一致する"), R.bHasOutline, WithOutline > 0);
	if (WithOutline > 0)
	{
		TestNotNull(TEXT("輪郭線コンポーネントが入っている"), OutlineTemplate);
	}
	else
	{
		TestNull(TEXT("輪郭線が無いモデルにはコンポーネントを付けない"), OutlineTemplate);
		AddWarning(TEXT("このモデルには輪郭線を描く材質がありません。")
			TEXT("【2】マテリアル変換を先に実行していない可能性があります。"));
	}

	// 作り直しても増殖しないこと (同じアセットを更新する)。
	const FMmdActorResult Again = FMmdActorBuilder::BuildActor(Mesh);
	TestTrue(TEXT("2 回目も成功する"), Again.bSuccess);
	TestEqual(TEXT("同じ Blueprint を更新する (増殖しない)"), Again.Blueprint, R.Blueprint);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
