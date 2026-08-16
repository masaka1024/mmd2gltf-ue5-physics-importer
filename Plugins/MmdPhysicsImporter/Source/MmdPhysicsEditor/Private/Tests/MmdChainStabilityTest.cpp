// Copyright (c) 2026 masaka1024. MIT License.
//
// 揺れ物の鎖が「伸び続けないか」を、アニメーションを再生した状態で見る。
//   MmdPhysics.Editor.ChainStability
//
//   MMD_PARITY_GLB      … mmd2gltf-gui が出力した .glb
//   MMD_CONV_SKELMESH   … 取り込み済み USkeletalMesh のパス
//   MMD_CHAIN_SECONDS   … 回す秒数 (既定 20)
//
// ★既存の MmdPhysics.Editor.WirePhysics との違いは 2 つ:
//     1. **アニメーションを再生する** (WirePhysics は静止のまま回す)。
//        駆動 (mode0) 剛体はアニメで動くので、鎖に外力が入る経路はここでしか通らない。
//     2. 見るのが移動量ではなく**ボーン間の距離**。
//
//   ジョイントの pos_min/pos_max が 0 の鎖 (MMD の揺れ物はほぼ全部そう) では、
//   ボーン間の距離は参照ポーズのまま保たれるのが正しい。ここが増え続けるのが
//   「しっぽが無限に落下する」の正体で、移動量だけを見ていると
//   「大きく動いた」と区別が付かない。
//
// ★体 (mode0) のボーンは物理で書き戻さないので、比べるのは物理ボーンだけ。

#include "Misc/AutomationTest.h"
#include "HAL/PlatformMisc.h"
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "MmdActorBuilder.h"
#include "MmdGlbPhysicsReader.h"
#include "MmdPhysicsWiring.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMmdChainStabilityTest, "MmdPhysics.Editor.ChainStability",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMmdChainStabilityTest::RunTest(const FString& Parameters)
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

	float Seconds = 20.0f;
	{
		const FString S = FPlatformMisc::GetEnvironmentVariable(TEXT("MMD_CHAIN_SECONDS"));
		if (!S.IsEmpty()) Seconds = FCString::Atof(*S);
	}

	// 物理が配線されていないメッシュでも単体で走れるようにしておく。
	if (Mesh->GetPostProcessAnimBlueprint().Get() == nullptr)
	{
		const FMmdWireResult Wire = FMmdPhysicsWiring::WirePhysics(Mesh, GlbPath);
		AddInfo(Wire.Message);
		if (!TestTrue(TEXT("WirePhysics が成功する"), Wire.bSuccess)) return false;
	}

	float UnitScale = 0.0f;
	TArray<FString> Warnings;
	TSharedPtr<MmdPhysics::PmxPhysicsModel> Model =
		MmdPhysics::GlbPhysicsReader::LoadFile(GlbPath, UnitScale, Warnings);
	if (!Model.IsValid())
	{
		AddError(TEXT(".glb の物理データを読めない"));
		return false;
	}

	const FReferenceSkeleton& RefSkel = Mesh->GetRefSkeleton();

	// 参照ポーズ (物理を通していない姿勢) をコンポーネント空間で用意する。
	TArray<FTransform> RefCS;
	{
		const TArray<FTransform>& LocalPose = RefSkel.GetRefBonePose();
		RefCS.SetNum(RefSkel.GetNum());
		for (int32 b = 0; b < RefSkel.GetNum(); b++)
		{
			const int32 Parent = RefSkel.GetParentIndex(b);
			RefCS[b] = (Parent == INDEX_NONE) ? LocalPose[b] : LocalPose[b] * RefCS[Parent];
		}
	}

	// --- 見る対象: 物理 (mode!=0) 剛体が付いたボーンのうち、親も骨格にあるもの ---
	struct FChainBone
	{
		int32 Bone = INDEX_NONE;
		int32 Parent = INDEX_NONE;
		FString Name;
		double RefLen = 0.0;
	};
	TArray<FChainBone> Chain;
	for (const MmdPhysics::PmxRigidBody& Rb : Model->RigidBodies)
	{
		if (Rb.PhysicsMode == 0) continue;
		if (!Model->BoneNames.IsValidIndex(Rb.BoneIndex)) continue;
		const int32 b = RefSkel.FindBoneIndex(FName(*Model->BoneNames[Rb.BoneIndex]));
		if (b == INDEX_NONE) continue;
		const int32 p = RefSkel.GetParentIndex(b);
		if (p == INDEX_NONE) continue;

		FChainBone C;
		C.Bone = b;
		C.Parent = p;
		C.Name = Model->BoneNames[Rb.BoneIndex];
		C.RefLen = FVector::Dist(RefCS[b].GetLocation(), RefCS[p].GetLocation());
		// 親と同じ位置のボーン (長さ 0) は比率で見られないので除く。
		if (C.RefLen > 0.1) Chain.Add(C);
	}
	if (Chain.Num() == 0)
	{
		AddWarning(TEXT("長さを測れる物理ボーンがありません。検証をスキップします。"));
		return true;
	}

	// --- ワールドを作ってアニメーションを再生しながら回す ---
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (World == nullptr) { AddError(TEXT("テスト用ワールドを作れない")); return false; }
	FWorldContext& Ctx = GEngine->CreateNewWorldContext(EWorldType::Game);
	Ctx.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	World->BeginPlay();

	AActor* Actor = World->SpawnActor<AActor>();
	USkeletalMeshComponent* Comp = NewObject<USkeletalMeshComponent>(Actor);
	Comp->SetSkeletalMeshAsset(Mesh);
	Comp->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	Comp->SetupAttachment(Actor->GetRootComponent());
	Comp->RegisterComponent();

	// ★アクター生成と同じ形でモーションを流す (FMmdActorBuilder::BuildActor 参照)。
	//   これを入れないと駆動剛体が動かず、鎖に外力が入らない。
	//   MMD_CHAIN_NOANIM=1 で静止のまま回せる (アニメが引き金かの切り分け用)。
	int32 AnimCandidates = 0;
	const bool bNoAnim = FPlatformMisc::GetEnvironmentVariable(TEXT("MMD_CHAIN_NOANIM")) == TEXT("1");
	UAnimSequence* Anim = bNoAnim ? nullptr : FMmdActorBuilder::FindAnimationFor(Mesh, AnimCandidates);
	if (Anim != nullptr)
	{
		Comp->SetAnimationMode(EAnimationMode::AnimationSingleNode);
		Comp->SetAnimation(Anim);
		Comp->Play(true);
	}
	//   MMD_CHAIN_NOPHYS=1 で物理 (Post-Process AnimBP) を切れる。
	//   アニメーション自体が鎖を伸ばしているのかの切り分け用。
	const bool bNoPhys = FPlatformMisc::GetEnvironmentVariable(TEXT("MMD_CHAIN_NOPHYS")) == TEXT("1");
	if (bNoPhys) Comp->SetDisablePostProcessBlueprint(true);

	AddInfo(FString::Printf(TEXT("モーション: %s / 物理: %s / 測るボーン %d 本 / %.1f 秒"),
		Anim != nullptr ? *Anim->GetName() : TEXT("なし (静止のまま回します)"),
		bNoPhys ? TEXT("切") : TEXT("入"), Chain.Num(), Seconds));

	auto Step = [Comp](float Dt)
	{
		Comp->TickAnimation(Dt, /*bNeedsValidRootMotion=*/false);
		Comp->RefreshBoneTransforms();
	};

	const float Dt = 1.0f / 30.0f;
	const int32 Steps = FMath::Max(1, FMath::RoundToInt(Seconds / Dt));

	// 起動直後は再整合が走るので、1 回回してから測り始める。
	Step(Dt);

	// 各ボーンについて、参照ポーズ長に対する比の最大と最小を追う。
	//
	// ★伸びだけでなく**縮み**も見ること。
	//   親フレームを取り違えて焼かれた鎖は、末端が伸びる一方で
	//   その手前が縮む (実測: しっぽ１３ が 23 倍のとき しっぽ１２ は 0.53 倍)。
	//   最大比だけ見ていると後者を取りこぼす。
	TArray<double> MaxRatio, MinRatio;
	MaxRatio.Init(1.0, Chain.Num());
	MinRatio.Init(1.0, Chain.Num());
	for (int32 s = 0; s < Steps; s++)
	{
		Step(Dt);
		for (int32 c = 0; c < Chain.Num(); c++)
		{
			const FVector A = Comp->GetBoneTransform(Chain[c].Bone, FTransform::Identity).GetLocation();
			const FVector B = Comp->GetBoneTransform(Chain[c].Parent, FTransform::Identity).GetLocation();
			const double Len = FVector::Dist(A, B);
			if (!FMath::IsFinite(Len)) continue;
			const double Ratio = Len / Chain[c].RefLen;
			MaxRatio[c] = FMath::Max(MaxRatio[c], Ratio);
			MinRatio[c] = FMath::Min(MinRatio[c], Ratio);
		}
	}

	// 並べ替え用に「1.0 からの外れ具合」を作る (伸びも縮みも同じ土俵で見る)。
	TArray<double> Deviation;
	Deviation.SetNum(Chain.Num());
	for (int32 c = 0; c < Chain.Num(); c++)
	{
		Deviation[c] = FMath::Max(MaxRatio[c], MinRatio[c] > 0.0 ? 1.0 / MinRatio[c] : TNumericLimits<double>::Max());
	}

	// 外れた順に並べて上位を出す。
	TArray<int32> Order;
	for (int32 c = 0; c < Chain.Num(); c++) Order.Add(c);
	Order.Sort([&Deviation](int32 A, int32 B) { return Deviation[A] > Deviation[B]; });

	AddInfo(TEXT("ボーン間距離の比 (参照ポーズ = 1.00):"));
	for (int32 k = 0; k < FMath::Min(10, Order.Num()); k++)
	{
		const int32 c = Order[k];
		AddInfo(FString::Printf(TEXT("  %-20s 最大 %.2f 倍 / 最小 %.2f 倍 (参照 %.2f cm)"),
			*Chain[c].Name, MaxRatio[c], MinRatio[c], Chain[c].RefLen));
	}

	// ★しきい値。物理焼きの並進 (剛体が付いたボーンの押し出し) は正当なデータなので
	//   1.0 ちょうどは要求できないが、エクスポーター側の仕様では「自分のバインド基準で
	//   比 1.0 近傍」であることが保証されている。2 倍 / 1/2 倍は明らかに破綻。
	//
	// ★限界: ここで見ているのは**長さだけ**で、向き (親フレーム) の誤りは検出できない。
	//   親を取り違えて焼かれても長さが偶然一致する場合がある
	//   (実測: 右腰ベルト４ は長さが合っていて向きだけ壊れていた)。
	//   最終確認は実機再生に残る。
	const int32 Worst = Order[0];
	TestTrue(FString::Printf(TEXT("鎖の長さが保たれている (最悪 '%s' が 最大 %.2f 倍 / 最小 %.2f 倍)"),
		*Chain[Worst].Name, MaxRatio[Worst], MinRatio[Worst]),
		MaxRatio[Worst] < 2.0 && MinRatio[Worst] > 0.5);

	Comp->UnregisterComponent();
	World->DestroyWorld(false);
	GEngine->DestroyWorldContext(World);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
