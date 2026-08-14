// Copyright (c) 2026 masaka1024. MIT License.
//
// 検証B: 実際に UE へ取り込んだスケルトンと extras.mmd の座標系が一致することを確かめる。
//
// 環境変数で対象を与える (未設定ならスキップ):
//   MMD_PARITY_GLB       … mmd2gltf-gui が出力した .glb
//   MMD_CONV_SKELMESH    … UE 標準の Interchange glTF で取り込んだ USkeletalMesh のパス
//                          (例 /Game/IA/IA)
//
// ★変換式はここに独立して書き下す。FMmdUeSpace を呼んでしまうと、
//   実装が間違っていても同じ間違いで照合してしまい検証にならない。

#include "Misc/AutomationTest.h"
#include "HAL/PlatformMisc.h"
#include "Engine/SkeletalMesh.h"
#include "ReferenceSkeleton.h"
#include "MmdGlbPhysicsReader.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace MmdPhysics;

namespace
{
	/** 取り込み経路の候補。位置の軸並べ替えだけで区別できる。 */
	struct FCandidate
	{
		const TCHAR* Name;
		FVector (*Map)(const Vec3&, float);
	};

	// 期待値: PMX(左手Y-up) → glTF(右手Y-up, Z反転) → UE(左手Z-up, Y/Z入替) → cm
	FVector MapExpected(const Vec3& V, float S) { return FVector(V.x * S, -V.z * S, V.y * S); }
	FVector MapNoZFlip(const Vec3& V, float S) { return FVector(V.x * S, V.z * S, V.y * S); }
	FVector MapIdentity(const Vec3& V, float S) { return FVector(V.x * S, V.y * S, V.z * S); }
	FVector MapGltfRuntime(const Vec3& V, float S) { return FVector(-V.z * S, V.x * S, V.y * S); }

	const FCandidate GCandidates[] =
	{
		{ TEXT("UE 標準の Interchange glTF (期待値)"), &MapExpected },
		{ TEXT("Z 反転なし"),                          &MapNoZFlip },
		{ TEXT("軸変換なし (PMX 生値)"),               &MapIdentity },
		{ TEXT("glTFRuntime の基底"),                  &MapGltfRuntime },
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMmdImportConventionTest, "MmdPhysics.Bridge.ImportConvention",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMmdImportConventionTest::RunTest(const FString& Parameters)
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

	float UnitScale = 0.0f;
	TArray<FString> Warnings;
	TSharedPtr<PmxPhysicsModel> Model = GlbPhysicsReader::LoadFile(GlbPath, UnitScale, Warnings);
	if (!Model.IsValid())
	{
		AddError(FString::Printf(TEXT("GLB を読めない: %s"), *GlbPath));
		return false;
	}

	const FReferenceSkeleton& RefSkel = Mesh->GetRefSkeleton();
	const int32 NumRefBones = RefSkel.GetNum();
	AddInfo(FString::Printf(TEXT("スケルトン %d ボーン / extras.mmd %d ボーン (unitScale=%g)"),
		NumRefBones, Model->BoneNames.Num(), UnitScale));

	// --- 参照ポーズをコンポーネント空間へ展開 ---
	const TArray<FTransform>& LocalPose = RefSkel.GetRefBonePose();
	TArray<FTransform> RefCS;
	RefCS.SetNum(NumRefBones);
	for (int32 b = 0; b < NumRefBones; b++)
	{
		const int32 Parent = RefSkel.GetParentIndex(b);
		RefCS[b] = (Parent == INDEX_NONE) ? LocalPose[b] : LocalPose[b] * RefCS[Parent];
	}

	// --- ボーン名の解決率 (日本語名が Interchange を通って残っているか) ---
	const float S = UnitScale * 100.0f;
	const int32 NumCandidates = UE_ARRAY_COUNT(GCandidates);
	TArray<float> MaxErr; MaxErr.Init(0.0f, NumCandidates);
	int32 Matched = 0;
	TArray<FString> Unresolved;

	for (int32 i = 0; i < Model->BoneNames.Num(); i++)
	{
		const int32 MeshBoneIndex = RefSkel.FindBoneIndex(FName(*Model->BoneNames[i]));
		if (MeshBoneIndex == INDEX_NONE)
		{
			Unresolved.Add(Model->BoneNames[i]);
			continue;
		}
		Matched++;
		const FVector Actual = RefCS[MeshBoneIndex].GetLocation();
		for (int32 c = 0; c < NumCandidates; c++)
		{
			MaxErr[c] = FMath::Max(MaxErr[c],
				static_cast<float>(FVector::Dist(GCandidates[c].Map(Model->BonePositions[i], S), Actual)));
		}
	}

	AddInfo(FString::Printf(TEXT("ボーン名の解決: %d / %d"), Matched, Model->BoneNames.Num()));
	if (Unresolved.Num() > 0)
	{
		const int32 Show = FMath::Min(Unresolved.Num(), 10);
		FString Sample;
		for (int32 i = 0; i < Show; i++) { if (i > 0) Sample += TEXT(", "); Sample += Unresolved[i]; }
		AddWarning(FString::Printf(TEXT("未解決 %d 件 (例: %s)"), Unresolved.Num(), *Sample));
	}

	for (int32 c = 0; c < NumCandidates; c++)
	{
		AddInfo(FString::Printf(TEXT("  候補[%s] 最大差 %.4f cm"), GCandidates[c].Name, MaxErr[c]));
	}

	int32 Best = 0;
	for (int32 c = 1; c < NumCandidates; c++) if (MaxErr[c] < MaxErr[Best]) Best = c;

	// --- 判定 ---
	TestTrue(TEXT("extras.mmd のボーン名がスケルトンに解決できる (半数以上)"), Matched * 2 >= Model->BoneNames.Num());
	TestEqual(TEXT("最も一致する取り込み経路が UE 標準の Interchange glTF である"),
		FString(GCandidates[Best].Name), FString(GCandidates[0].Name));
	TestTrue(FString::Printf(TEXT("参照ポーズとの最大差が小さい (%.4f cm <= 1.0 cm)"), MaxErr[0]), MaxErr[0] <= 1.0f);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
