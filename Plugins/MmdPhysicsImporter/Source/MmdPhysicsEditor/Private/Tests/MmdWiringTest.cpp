// Copyright (c) 2026 masaka1024. MIT License.
//
// 検証C(自動化できる範囲): 【1】物理を配線 が実際に効くことを、エディタを開かずに確かめる。
//
//   MMD_PARITY_GLB     … mmd2gltf-gui が出力した .glb
//   MMD_CONV_SKELMESH  … 取り込み済み USkeletalMesh のパス (例 /Game/IA/IA)
//
// 1) WirePhysics が Post-Process Anim Blueprint を作って割り当てるか
// 2) その状態でワールドを回したとき、揺れ物ボーンが実際に動き、体のボーンは動かないか
//
// 2) が通れば「AnimGraph の配線・空間変換ノードの自動挿入・ボーン解決・書き戻し」が
// 一通り機能していることになる。見た目の妥当性 (髪が体を貫通しないか等) は
// エディタでの目視に残る。

#include "Misc/AutomationTest.h"
#include "HAL/PlatformMisc.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"
#include "Animation/AnimBlueprint.h"
#include "MmdPhysicsWiring.h"
#include "MmdGlbPhysicsReader.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMmdWiringTest, "MmdPhysics.Editor.WirePhysics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMmdWiringTest::RunTest(const FString& Parameters)
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

	// --- 1) 配線 ---
	const FMmdWireResult Wire = FMmdPhysicsWiring::WirePhysics(Mesh, GlbPath);
	AddInfo(Wire.Message);
	if (!TestTrue(TEXT("WirePhysics が成功する"), Wire.bSuccess)) return false;
	if (!TestNotNull(TEXT("Post-Process Anim Blueprint が割り当てられている"),
		Mesh->GetPostProcessAnimBlueprint().Get())) return false;

	// --- 2) ワールドを回して、揺れ物が動き体が動かないことを見る ---
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (World == nullptr) { AddError(TEXT("テスト用ワールドを作れない")); return false; }
	FWorldContext& Ctx = GEngine->CreateNewWorldContext(EWorldType::Game);
	Ctx.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	World->BeginPlay();

	AActor* Actor = World->SpawnActor<AActor>();
	USkeletalMeshComponent* Comp = NewObject<USkeletalMeshComponent>(Actor);
	Comp->SetSkeletalMesh(Mesh);
	// 描画されていなくてもポーズを評価させる (nullrhi のヘッドレス実行のため)。
	Comp->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	Comp->SetupAttachment(Actor->GetRootComponent());
	Comp->RegisterComponent();

	// 代表ボーンは名前で当てずに extras.mmd から選ぶ。
	//   揺れ物 = PhysicsMode 1 (物理演算) の剛体が付いたボーン
	//   体     = PhysicsMode 0 (ボーン追従) の剛体が付いたボーン
	// 髪やスカートでもチェーンの根元は mode0 (kinematic) であることが多く、
	// 名前で選ぶと「動かないのが正しいボーン」を掴んでしまう。
	const FReferenceSkeleton& RefSkel = Mesh->GetRefSkeleton();
	int32 SwayBone = INDEX_NONE, BodyBone = INDEX_NONE;
	{
		float ProbeUnitScale = 0.0f;
		TArray<FString> ProbeWarnings;
		TSharedPtr<MmdPhysics::PmxPhysicsModel> Probe =
			MmdPhysics::GlbPhysicsReader::LoadFile(GlbPath, ProbeUnitScale, ProbeWarnings);
		if (Probe.IsValid())
		{
			for (const MmdPhysics::PmxRigidBody& Rb : Probe->RigidBodies)
			{
				if (!Probe->BoneNames.IsValidIndex(Rb.BoneIndex)) continue;
				const int32 b = RefSkel.FindBoneIndex(FName(*Probe->BoneNames[Rb.BoneIndex]));
				if (b == INDEX_NONE) continue;
				if (SwayBone == INDEX_NONE && Rb.PhysicsMode == 1) SwayBone = b;
				if (BodyBone == INDEX_NONE && Rb.PhysicsMode == 0) BodyBone = b;
			}
		}
	}
	if (SwayBone == INDEX_NONE)
	{
		AddWarning(TEXT("PhysicsMode=1 の剛体が見つからないため、動きの確認は行いません。"));
	}
	AddInfo(FString::Printf(TEXT("揺れ物ボーン=%s / 体ボーン=%s"),
		SwayBone != INDEX_NONE ? *RefSkel.GetBoneName(SwayBone).ToString() : TEXT("-"),
		BodyBone != INDEX_NONE ? *RefSkel.GetBoneName(BodyBone).ToString() : TEXT("-")));

	auto BonePos = [Comp, &RefSkel](int32 Idx) -> FVector
	{
		return Idx == INDEX_NONE ? FVector::ZeroVector
			: Comp->GetBoneTransform(Idx, FTransform::Identity).GetLocation();
	};

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

	AddInfo(FString::Printf(TEXT("Post-Process AnimInstance: %s / メイン AnimInstance: %s"),
		Comp->GetPostProcessInstance() != nullptr ? TEXT("あり") : TEXT("なし"),
		Comp->GetAnimInstance() != nullptr ? TEXT("あり") : TEXT("なし")));

	// ★ワールドの tick 経路 (可視性・tick グループ・URO) に依存すると
	//   ヘッドレスでポーズ評価ごと省かれることがあるので、直接駆動する。
	auto Step = [Comp](float Dt)
	{
		Comp->TickAnimation(Dt, /*bNeedsValidRootMotion=*/false);
		Comp->RefreshBoneTransforms();
	};

	// 1 回回して初期姿勢を確定させてから基準を取る (起動直後は再整合が走るため)。
	Step(1.0f / 30.0f);
	const FVector SwayStart = BonePos(SwayBone);
	const FVector BodyStart = BonePos(BodyBone);

	for (int32 i = 0; i < 60; i++)
	{
		Step(1.0f / 30.0f);
	}

	const double SwayMoved = FVector::Dist(SwayStart, BonePos(SwayBone));
	const double BodyMoved = FVector::Dist(BodyStart, BonePos(BodyBone));

	// ★アニメーションを与えていないので、バインドポーズは既にほぼ釣り合っている。
	//   大きく動かないのが物理的に正しい (MmdPhysics.Core.Equilibrium と同じ性質)。
	//   ここで見たいのは「書き戻しが物理ボーン群へ届いているか」なので、
	//   移動量の大小ではなく「動いたボーンの数」と「動かないはずのボーンが動いていないこと」で判定する。
	int32 MovedPhysBones = 0, MovedBodyBones = 0, NumPhys = 0, NumBody = 0;
	{
		float ProbeUnitScale = 0.0f;
		TArray<FString> ProbeWarnings;
		TSharedPtr<MmdPhysics::PmxPhysicsModel> Probe =
			MmdPhysics::GlbPhysicsReader::LoadFile(GlbPath, ProbeUnitScale, ProbeWarnings);
		if (Probe.IsValid())
		{
			for (const MmdPhysics::PmxRigidBody& Rb : Probe->RigidBodies)
			{
				if (!Probe->BoneNames.IsValidIndex(Rb.BoneIndex)) continue;
				const int32 b = RefSkel.FindBoneIndex(FName(*Probe->BoneNames[Rb.BoneIndex]));
				if (b == INDEX_NONE) continue;
				// 参照ポーズ (物理を通していない姿勢) と、いま評価された姿勢を比べる。
				const FVector RefPos = RefCS[b].GetLocation();
				const double Delta = FVector::Dist(RefPos, BonePos(b));
				if (Rb.PhysicsMode == 0) { NumBody++; if (Delta > 1e-3) MovedBodyBones++; }
				else { NumPhys++; if (Delta > 1e-3) MovedPhysBones++; }
			}
		}
	}

	AddInfo(FString::Printf(TEXT("2秒後の移動量: 揺れ物 %.4f cm / 体 %.4f cm"), SwayMoved, BodyMoved));
	AddInfo(FString::Printf(TEXT("参照ポーズから動いた剛体ボーン: 物理 %d/%d / 追従 %d/%d"),
		MovedPhysBones, NumPhys, MovedBodyBones, NumBody));

	// 物理ボーンの過半が参照ポーズから動いていれば、書き戻しが届いている。
	// ノードが評価されていない場合はここが 0 になる。
	TestTrue(FString::Printf(TEXT("物理ボーンへ書き戻しが届いている (%d/%d)"), MovedPhysBones, NumPhys),
		NumPhys > 0 && MovedPhysBones * 2 > NumPhys);

	// BoneFollow の剛体には書き戻さないので、体はアニメーションのまま (今回は静止) のはず。
	TestEqual(TEXT("ボーン追従の剛体は物理で動かされていない"), MovedBodyBones, 0);

	if (SwayBone != INDEX_NONE)
	{
		TestTrue(FString::Printf(TEXT("揺れ物が発散していない (%.4f cm < 500)"), SwayMoved), SwayMoved < 500.0);
	}
	TestTrue(FString::Printf(TEXT("体のボーンは動いていない (%.4f cm < 0.01)"), BodyMoved), BodyMoved < 0.01);

	Comp->UnregisterComponent();
	World->DestroyWorld(false);
	GEngine->DestroyWorldContext(World);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
