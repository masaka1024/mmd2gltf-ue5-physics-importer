// Copyright (c) 2026 masaka1024. MIT License.

#include "MmdFollowerMeshComponent.h"

#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInterface.h"

UMmdFollowerMeshComponent::UMmdFollowerMeshComponent()
{
	// モーフのウェイトを本体から写すために毎フレーム回す。
	PrimaryComponentTick.bCanEverTick = true;
	// ★本体の後に回さないと、1 フレーム古いウェイトを写してしまう。
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
	// エディタのビューポートでも回す (表情を動かしたとき追従させるため)。
	bTickInEditor = true;

	// 見た目だけの存在。当たり判定も影も持たせない。
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetGenerateOverlapEvents(false);
	bCastDynamicShadow = false;
	CastShadow = false;
	// ボーンは本体から貰うので、自分でアニメーションは評価しない。
	SetAnimationMode(EAnimationMode::AnimationCustomMode);
	VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
}

USkeletalMeshComponent* UMmdFollowerMeshComponent::FindLeader() const
{
	// 親コンポーネント → 同じアクターの最初のスケルタルメッシュ、の順で探す。
	if (USkeletalMeshComponent* Parent = Cast<USkeletalMeshComponent>(GetAttachParent()))
	{
		return Parent;
	}
	if (AActor* Owner = GetOwner())
	{
		TArray<USkeletalMeshComponent*> Components;
		Owner->GetComponents(Components);
		for (USkeletalMeshComponent* Comp : Components)
		{
			if (Comp != this && !Comp->IsA<UMmdFollowerMeshComponent>())
			{
				return Comp;
			}
		}
	}
	return nullptr;
}

UMaterialInterface* UMmdFollowerMeshComponent::FindMasterMaterial(const TCHAR* AssetName) const
{
	USkeletalMesh* Mesh = GetSkeletalMeshAsset();
	if (Mesh == nullptr) return nullptr;
	const FString Folder = FPackageName::GetLongPackagePath(Mesh->GetOutermost()->GetName());
	const FString Path = FString::Printf(TEXT("%s/%s.%s"), *Folder, AssetName, AssetName);
	return LoadObject<UMaterialInterface>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
}

void UMmdFollowerMeshComponent::OnRegister()
{
	Super::OnRegister();

	USkeletalMeshComponent* Leader = FindLeader();
	if (Leader == nullptr) return;

	// 同じメッシュを描く。これでモーフもボーン構成も本体と完全に一致する。
	if (GetSkeletalMeshAsset() != Leader->GetSkeletalMeshAsset())
	{
		SetSkeletalMeshAsset(Leader->GetSkeletalMeshAsset());
	}

	// ボーンは本体の結果 (MMD 物理を通したもの) をそのまま使う。
	if (LeaderPoseComponent.Get() != Leader)
	{
		SetLeaderPoseComponent(Leader);
	}

	RebuildMaterials();
}

void UMmdFollowerMeshComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// ★モーフだけは LeaderPoseComponent が運んでくれない (ボーンだけ)。
	//   同じメッシュを参照しているので、ウェイトの配列は添字がそのまま一致する。
	//   ここを写さないと、表情を変えたときにこちらだけ元の顔のまま取り残される。
	if (USkeletalMeshComponent* Leader = FindLeader())
	{
		if (Leader->GetSkeletalMeshAsset() == GetSkeletalMeshAsset()
			&& Leader->MorphTargetWeights.Num() == MorphTargetWeights.Num())
		{
			MorphTargetWeights = Leader->MorphTargetWeights;
			ActiveMorphTargets = Leader->ActiveMorphTargets;
		}
	}
}

#if WITH_EDITOR
void UMmdFollowerMeshComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	// マテリアルは動的インスタンスなので、詳細パネルで値を触ったら作り直さないと反映されない。
	RebuildMaterials();
}
#endif
