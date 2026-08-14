// Copyright (c) 2026 masaka1024. MIT License.

#include "AnimNode_MmdPhysics.h"
#include "MmdUeSpace.h"
#include "MmdGlbPhysicsReader.h"
#include "MmdPhysicsCoreLog.h"
#include "Animation/AnimInstanceProxy.h"

using namespace MmdPhysics;

namespace
{
	FORCEINLINE bool IsFinite(const Vec3& v)
	{
		return FMath::IsFinite(v.x) && FMath::IsFinite(v.y) && FMath::IsFinite(v.z);
	}

	FORCEINLINE bool IsFinite(const Quat& q)
	{
		return FMath::IsFinite(q.x) && FMath::IsFinite(q.y) && FMath::IsFinite(q.z) && FMath::IsFinite(q.w);
	}

	/** 取り込み経路の候補。位置の軸並べ替えだけで区別できる。 */
	struct FConventionCandidate
	{
		const TCHAR* Name;
		FVector (*Map)(const Vec3&, float);
	};

	FVector MapInterchange(const Vec3& V, float S) { return FVector(V.x * S, -V.z * S, V.y * S); }  // UE 標準 (期待値)
	FVector MapNoZFlip(const Vec3& V, float S) { return FVector(V.x * S, V.z * S, V.y * S); }       // 変換側が Z を反転していない
	FVector MapIdentity(const Vec3& V, float S) { return FVector(V.x * S, V.y * S, V.z * S); }      // 変換を通していない
	FVector MapGltfRuntime(const Vec3& V, float S) { return FVector(-V.z * S, V.x * S, V.y * S); }  // glTFRuntime の既定基底

	const FConventionCandidate GCandidates[] =
	{
		{ TEXT("UE 標準の Interchange glTF (期待値)"), &MapInterchange },
		{ TEXT("Z 反転なし (変換側の設定違い)"),        &MapNoZFlip },
		{ TEXT("軸変換なし (PMX 生値のまま)"),          &MapIdentity },
		{ TEXT("glTFRuntime の基底"),                   &MapGltfRuntime },
	};
}

void FAnimNode_MmdPhysics::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	FAnimNode_SkeletalControlBase::Initialize_AnyThread(Context);
	AccumulatedDelta = 0.0f;
	StartupResetCountdown = FMath::Max(0, PoseResetDelayFrames);
	bNanReported = false;
}

void FAnimNode_MmdPhysics::UpdateInternal(const FAnimationUpdateContext& Context)
{
	FAnimNode_SkeletalControlBase::UpdateInternal(Context);
	// PhysicsWorld 側が固定刻みのアキュムレータを持っているので、フレームの実 dt をそのまま渡す。
	// 評価が間引かれる (URO 等) 場合に取りこぼさないよう、ここでは積むだけにする。
	AccumulatedDelta += Context.GetDeltaTime();
}

bool FAnimNode_MmdPhysics::IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones)
{
	return Builder.IsValid() && Builder->BoneLinks.Num() > 0;
}

void FAnimNode_MmdPhysics::EnsureLoaded()
{
	if (bLoadAttempted) return;
	bLoadAttempted = true;

	if (GlbPath.IsEmpty())
	{
		UE_LOG(LogMmdPhysics, Warning, TEXT("[MmdPhysics] GlbPath が空です。物理は無効のままになります。"));
		return;
	}

	float LoadedUnitScale = 0.0f;
	TArray<FString> Warnings;
	Model = GlbPhysicsReader::LoadFile(GlbPath, LoadedUnitScale, Warnings);
	for (const FString& W : Warnings)
	{
		UE_LOG(LogMmdPhysics, Warning, TEXT("[MmdPhysics] %s"), *W);
	}
	if (!Model.IsValid())
	{
		UE_LOG(LogMmdPhysics, Error, TEXT("[MmdPhysics] GLB を読めませんでした: %s"), *GlbPath);
		return;
	}

	// extras.mmd の unitScale とノード設定が食い違うと、剛体だけ別スケールで配置される。
	if (!FMath::IsNearlyEqual(LoadedUnitScale, UnitScale, 1e-6f))
	{
		UE_LOG(LogMmdPhysics, Warning,
			TEXT("[MmdPhysics] UnitScale がノード設定(%g)と extras.mmd(%g)で食い違います。extras.mmd の値を採用します。"),
			UnitScale, LoadedUnitScale);
		UnitScale = LoadedUnitScale;
	}

	Builder = PmxPhysicsBuilder::Build(Model);
	ApplySolverSettings();

	UE_LOG(LogMmdPhysics, Log,
		TEXT("[MmdPhysics] 読み込み完了: ボーン%d 剛体%d ジョイント%d (unitScale=%g) %s"),
		Model->BoneNames.Num(), Builder->Bodies.Num(), Builder->World.Joints.Num(), UnitScale, *GlbPath);
}

void FAnimNode_MmdPhysics::ApplySolverSettings()
{
	if (!Builder.IsValid()) return;
	// ★毎評価で反映する。読み込み時に一度だけ書くと、AnimBP でノードの値を変えても
	//   （再読み込みが走らない限り）何も起きない。設定が効かない、という形で静かに壊れる。
	Builder->World.Gravity = Vec3(0.0f, -Gravity, 0.0f);
	Builder->World.SolverIterations = FMath::Max(1, SolverIterations);
	Builder->World.SubSteps = FMath::Max(1, SubSteps);
	Builder->World.FixedTimeStep = FMath::Max(1e-4f, FixedTimeStep);
}

void FAnimNode_MmdPhysics::InitializeBoneReferences(const FBoneContainer& RequiredBones)
{
	EnsureLoaded();
	if (!Model.IsValid()) return;
	ResolveBones(RequiredBones);
	if (bCheckImportConvention && !bConventionChecked)
	{
		bConventionChecked = true;
		CheckImportConvention(RequiredBones);
	}
}

void FAnimNode_MmdPhysics::ResolveBones(const FBoneContainer& RequiredBones)
{
	const FReferenceSkeleton& RefSkel = RequiredBones.GetReferenceSkeleton();
	const int32 NumPmxBones = Model->BoneNames.Num();

	PmxBoneToCompact.Reset();
	PmxBoneToCompact.Init(FCompactPoseBoneIndex(INDEX_NONE), NumPmxBones);

	int32 Resolved = 0;
	TArray<FString> Unresolved;
	for (int32 i = 0; i < NumPmxBones; i++)
	{
		const FString& PmxName = Model->BoneNames[i];

		// 1) FName での完全一致 (FName は大文字小文字を区別しない)。
		int32 MeshBoneIndex = RefSkel.FindBoneIndex(FName(*PmxName));

		// 2) 見つからなければ大小文字を区別する線形探索でフォールバックする。
		//    Interchange が日本語ボーン名をサニタイズしたり、重複時にサフィックスを
		//    付けたりする可能性があるため。★採用する名前は必ず RefSkeleton 側のもの。
		if (MeshBoneIndex == INDEX_NONE)
		{
			for (int32 b = 0; b < RefSkel.GetNum(); b++)
			{
				if (RefSkel.GetBoneName(b).ToString().Equals(PmxName, ESearchCase::CaseSensitive))
				{
					MeshBoneIndex = b;
					break;
				}
			}
		}

		if (MeshBoneIndex == INDEX_NONE)
		{
			Unresolved.Add(PmxName);
			continue;
		}

		const FCompactPoseBoneIndex Compact = RequiredBones.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshBoneIndex));
		PmxBoneToCompact[i] = Compact;
		if (Compact.GetInt() != INDEX_NONE) Resolved++;
	}

	if (Unresolved.Num() > 0)
	{
		// 全部並べるとログが埋まるので先頭だけ名指しする。
		const int32 Show = FMath::Min(Unresolved.Num(), 8);
		FString Sample;
		for (int32 i = 0; i < Show; i++) { if (i > 0) Sample += TEXT(", "); Sample += Unresolved[i]; }
		UE_LOG(LogMmdPhysics, Warning,
			TEXT("[MmdPhysics] スケルトンに見つからないボーンが %d 件あります (例: %s%s)。"
				"該当ボーンの物理は適用されません。"),
			Unresolved.Num(), *Sample, Unresolved.Num() > Show ? TEXT(", ...") : TEXT(""));
	}
	UE_LOG(LogMmdPhysics, Log, TEXT("[MmdPhysics] ボーン解決: %d / %d"), Resolved, NumPmxBones);
}

void FAnimNode_MmdPhysics::CheckImportConvention(const FBoneContainer& RequiredBones)
{
	const FReferenceSkeleton& RefSkel = RequiredBones.GetReferenceSkeleton();
	const int32 NumRefBones = RefSkel.GetNum();
	if (NumRefBones == 0 || Model->BonePositions.Num() == 0) return;

	// 参照ポーズをコンポーネント空間へ展開する。
	const TArray<FTransform>& LocalPose = RefSkel.GetRefBonePose();
	TArray<FTransform> RefCS;
	RefCS.SetNum(NumRefBones);
	for (int32 b = 0; b < NumRefBones; b++)
	{
		const int32 Parent = RefSkel.GetParentIndex(b);
		RefCS[b] = (Parent == INDEX_NONE) ? LocalPose[b] : LocalPose[b] * RefCS[Parent];
	}

	const float S = FMmdUeSpace::LengthScale(UnitScale);

	// 候補ごとに「extras.mmd のボーン位置を変換した点」と「参照ポーズの点」の最大差を測る。
	const int32 NumCandidates = UE_ARRAY_COUNT(GCandidates);
	TArray<float> MaxErr;
	MaxErr.Init(0.0f, NumCandidates);
	int32 Compared = 0;

	for (int32 i = 0; i < Model->BonePositions.Num(); i++)
	{
		const FString& PmxName = Model->BoneNames[i];
		const int32 MeshBoneIndex = RefSkel.FindBoneIndex(FName(*PmxName));
		if (MeshBoneIndex == INDEX_NONE) continue;
		const FVector Actual = RefCS[MeshBoneIndex].GetLocation();
		Compared++;
		for (int32 c = 0; c < NumCandidates; c++)
		{
			const float Err = static_cast<float>(FVector::Dist(GCandidates[c].Map(Model->BonePositions[i], S), Actual));
			MaxErr[c] = FMath::Max(MaxErr[c], Err);
		}
	}

	if (Compared == 0)
	{
		UE_LOG(LogMmdPhysics, Warning,
			TEXT("[MmdPhysics] 取り込み経路を検査できません (extras.mmd のボーン名がスケルトンと1つも一致しません)。"));
		return;
	}

	int32 Best = 0;
	for (int32 c = 1; c < NumCandidates; c++) if (MaxErr[c] < MaxErr[Best]) Best = c;

	UE_LOG(LogMmdPhysics, Log,
		TEXT("[MmdPhysics] 取り込み経路の検査: %d 本を照合。期待値(UE標準 Interchange glTF)の最大差 %.4f cm。"),
		Compared, MaxErr[0]);

	if (Best != 0)
	{
		UE_LOG(LogMmdPhysics, Error,
			TEXT("[MmdPhysics] ★スケルトンが「%s」の状態で取り込まれています ")
			TEXT("(その仮定なら最大差 %.4f cm、UE 標準を仮定すると %.4f cm)。")
			TEXT("このままでは髪やスカートが体と逆方向へ出ます。")
			TEXT("対処: .glb を UE 標準の Interchange glTF インポータで取り込み直してください ")
			TEXT("(glTFRuntime や非推奨の旧 GLTFImporter、FBX 経由では座標系が一致しません)。"),
			GCandidates[Best].Name, MaxErr[Best], MaxErr[0]);
	}
	else if (MaxErr[0] > 1.0f)
	{
		UE_LOG(LogMmdPhysics, Warning,
			TEXT("[MmdPhysics] 座標系は一致していますが、参照ポーズとの最大差が %.4f cm あります。")
			TEXT("UnitScale (%g) が extras.mmd と食い違っていないか確認してください。"),
			MaxErr[0], UnitScale);
	}
}

void FAnimNode_MmdPhysics::EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output, TArray<FBoneTransform>& OutBoneTransforms)
{
	if (!Builder.IsValid()) return;

	// ノードの設定を毎回反映する (エディタで値を変えたらすぐ効くように)。
	ApplySolverSettings();

	// 移植元 BoneWorldOrNull 相当。未解決ボーンは未設定を返す (前回ターゲット維持=テレポートしない)。
	auto BoneWorldOrNull = [this, &Output](int32 BoneIndex) -> TOptional<RigidTransform>
	{
		if (!PmxBoneToCompact.IsValidIndex(BoneIndex)) return TOptional<RigidTransform>();
		const FCompactPoseBoneIndex Idx = PmxBoneToCompact[BoneIndex];
		if (Idx.GetInt() == INDEX_NONE) return TOptional<RigidTransform>();
		return TOptional<RigidTransform>(FMmdUeSpace::ToMmd(Output.Pose.GetComponentSpaceTransform(Idx), UnitScale));
	};

	// 起動直後: アニメがフレーム0を適用した後の骨格へ物理を再整合する。
	if (StartupResetCountdown > 0)
	{
		Builder->ResetBodiesToBonePose(BoneWorldOrNull);
		StartupResetCountdown--;
	}

	// 駆動 (BoneFollow) 剛体の kinematic ターゲットを押し込む。
	Builder->ApplyKinematicTargets(BoneWorldOrNull);

	if (AccumulatedDelta > 0.0f)
	{
		Builder->World.StepSimulation(AccumulatedDelta);
		AccumulatedDelta = 0.0f;
	}

	// --- 物理 -> ボーンの書き戻し (移植元 PullPhysicsToBones) ---
	const bool bNeedAligned = bAlignBonePositions || (bEnableBoneMergeMode && Builder->HasBoneMergeBodies());
	TArray<TOptional<RigidTransform>> Aligned;
	if (bNeedAligned)
	{
		Aligned = Builder->ComputeAlignedBonePoses(BoneWorldOrNull, AlignRotClampAlpha);
	}

	for (const BoneLink& Link : Builder->BoneLinks)
	{
		if (Link.Mode == EPhysicsMode::BoneFollow) continue;   // 体は物理で上書きしない
		if (!PmxBoneToCompact.IsValidIndex(Link.BoneIndex)) continue;
		const FCompactPoseBoneIndex Idx = PmxBoneToCompact[Link.BoneIndex];
		if (Idx.GetInt() == INDEX_NONE) continue;

		// 補正を使うのは「全体トグルON」または「この剛体が mode2」のとき。
		const bool bUseAligned = bNeedAligned && Aligned.IsValidIndex(Link.BoneIndex) && Aligned[Link.BoneIndex].IsSet()
			&& (bAlignBonePositions || (bEnableBoneMergeMode && Link.Mode == EPhysicsMode::DynamicBoneMerge));

		// body = bone * offset  ->  bone = body * offset^-1
		const RigidTransform BoneWorld = bUseAligned
			? Aligned[Link.BoneIndex].GetValue()
			: Link.Body->WorldTransform * Link.BodyOffsetFromBone.Inverse();

		// ★NaN ガード。物理が発散した値をそのまま書くとスケルトンが壊れ、
		//   毎フレーム大量のエラーが出て原因も埋もれる。最初の1回だけ名指しで警告する。
		if (!IsFinite(BoneWorld.Origin) || !IsFinite(BoneWorld.Rotation))
		{
			if (!bNanReported)
			{
				bNanReported = true;
				const FString BoneName = Model->BoneNames.IsValidIndex(Link.BoneIndex)
					? Model->BoneNames[Link.BoneIndex] : FString::Printf(TEXT("#%d"), Link.BoneIndex);
				UE_LOG(LogMmdPhysics, Error,
					TEXT("[MmdPhysics] 物理が発散して NaN になりました (最初の検出: ボーン '%s')。")
					TEXT("このボーンへの書き戻しを停止し、以後の NaN は無視します。")
					TEXT("剛体のばね定数が大きすぎる/質量が小さすぎるモデルで起きます。")
					TEXT("FixedTimeStep(%.5f) を小さくするか SubSteps を増やすと改善することがあります。"),
					*BoneName, FixedTimeStep);
			}
			continue;
		}

		// スケールは入力ポーズのものを保つ (位置と回転だけ物理で置き換える)。
		const FTransform Current = Output.Pose.GetComponentSpaceTransform(Idx);
		OutBoneTransforms.Add(FBoneTransform(Idx, FMmdUeSpace::ToUe(BoneWorld, UnitScale, Current.GetScale3D())));
	}

	// LocalBlendCSBoneTransforms は index 昇順を要求する。
	OutBoneTransforms.Sort([](const FBoneTransform& A, const FBoneTransform& B)
	{
		return A.BoneIndex < B.BoneIndex;
	});
}

void FAnimNode_MmdPhysics::GatherDebugData(FNodeDebugData& DebugData)
{
	FString Line = FString::Printf(TEXT("MMD Physics: bodies=%d joints=%d bones=%d"),
		Builder.IsValid() ? Builder->Bodies.Num() : 0,
		Builder.IsValid() ? Builder->World.Joints.Num() : 0,
		Model.IsValid() ? Model->BoneNames.Num() : 0);
	DebugData.AddDebugItem(Line);
	ComponentPose.GatherDebugData(DebugData);
}
