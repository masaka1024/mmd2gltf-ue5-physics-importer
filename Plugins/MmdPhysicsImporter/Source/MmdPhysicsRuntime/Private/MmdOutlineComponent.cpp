// Copyright (c) 2026 masaka1024. MIT License.

#include "MmdOutlineComponent.h"

#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

UMmdOutlineComponent::UMmdOutlineComponent()
{
	// モーフのウェイトを本体から写すために毎フレーム回す。
	PrimaryComponentTick.bCanEverTick = true;
	// ★本体の後に回さないと、1 フレーム古いウェイトを写してしまう。
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
	// エディタのビューポートでも回す (表情を動かしたとき輪郭線も追従させるため)。
	bTickInEditor = true;

	// 輪郭線は見た目だけの存在。当たり判定も影も持たせない。
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetGenerateOverlapEvents(false);
	bCastDynamicShadow = false;
	CastShadow = false;
	// ボーンは本体から貰うので、自分でアニメーションは評価しない。
	SetAnimationMode(EAnimationMode::AnimationCustomMode);
	VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
}

USkeletalMeshComponent* UMmdOutlineComponent::FindLeader() const
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
			if (Comp != this && !Comp->IsA<UMmdOutlineComponent>())
			{
				return Comp;
			}
		}
	}
	return nullptr;
}

void UMmdOutlineComponent::OnRegister()
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

	RebuildOutlineMaterials();
}

void UMmdOutlineComponent::RebuildOutlineMaterials()
{
	USkeletalMeshComponent* Leader = FindLeader();
	USkeletalMesh* Mesh = GetSkeletalMeshAsset();
	if (Leader == nullptr || Mesh == nullptr) return;

	UMaterialInterface* Master = OutlineMaterial;
	if (Master == nullptr)
	{
		// マテリアル変換がメッシュと同じフォルダに作る。
		const FString Folder = FPackageName::GetLongPackagePath(Mesh->GetOutermost()->GetName());
		const FString Path = Folder / TEXT("M_MmdOutline.M_MmdOutline");
		Master = LoadObject<UMaterialInterface>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
	}
	if (Master == nullptr)
	{
		// 素材が無ければ何もしない。輪郭線が出ないだけで本体には影響しない。
		return;
	}

	const int32 NumSlots = Mesh->GetMaterials().Num();
	for (int32 Slot = 0; Slot < NumSlots; Slot++)
	{
		// 材質ごとの輪郭線の色と太さは、本体のマテリアルインスタンスが持っている
		// (マテリアル変換が EdgeColor / EdgeSize / UseOutline を入れてある)。
		FLinearColor EdgeColor = FLinearColor::Black;
		float EdgeSize = 1.0f;
		float UseOutline = 0.0f;
		if (UMaterialInterface* Body = Leader->GetMaterial(Slot))
		{
			Body->GetVectorParameterValue(FMaterialParameterInfo(TEXT("EdgeColor")), EdgeColor);
			Body->GetScalarParameterValue(FMaterialParameterInfo(TEXT("EdgeSize")), EdgeSize);
			Body->GetScalarParameterValue(FMaterialParameterInfo(TEXT("UseOutline")), UseOutline);
		}

		UMaterialInstanceDynamic* Mid = CreateDynamicMaterialInstance(Slot, Master);
		if (Mid == nullptr) continue;
		Mid->SetVectorParameterValue(TEXT("EdgeColor"), EdgeColor);
		Mid->SetScalarParameterValue(TEXT("EdgeSize"), EdgeSize);
		Mid->SetScalarParameterValue(TEXT("OutlineWidthScale"), OutlineWidthScale);
		Mid->SetScalarParameterValue(TEXT("UseOutline"), (bDrawOutline && UseOutline > 0.5f) ? 1.0f : 0.0f);
	}
}

void UMmdOutlineComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// ★モーフだけは LeaderPoseComponent が運んでくれない (ボーンだけ)。
	//   同じメッシュを参照しているので、ウェイトの配列は添字がそのまま一致する。
	//   ここを写さないと、表情を変えたときに輪郭線だけ元の顔のまま取り残される。
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
