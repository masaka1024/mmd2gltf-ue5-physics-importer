// Copyright (c) 2026 masaka1024. MIT License.

#include "MmdPhysicsFunctionLibrary.h"

#include "AnimNode_MmdPhysics.h"
#include "Animation/AnimClassInterface.h"
#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "MmdPhysicsCoreLog.h"

namespace
{
	/** そのアニメーションインスタンスが持つ MMD 物理ノードすべてに再整合を要求する。 */
	int32 RequestResetOn(UAnimInstance* Instance)
	{
		if (Instance == nullptr) return 0;

		IAnimClassInterface* AnimClass = IAnimClassInterface::GetFromClass(Instance->GetClass());
		if (AnimClass == nullptr) return 0;

		int32 Count = 0;
		for (const FStructProperty* NodeProperty : AnimClass->GetAnimNodeProperties())
		{
			if (NodeProperty == nullptr || NodeProperty->Struct == nullptr) continue;
			if (!NodeProperty->Struct->IsChildOf(FAnimNode_MmdPhysics::StaticStruct())) continue;

			// ★ゲームスレッドから int32 のカウンタを立てるだけ。評価はワーカースレッドで走るが、
			//   ここで書くのは「あと何フレーム再整合するか」の 1 つだけで、
			//   取りこぼしても次のフレームで立て直せる値なので排他はしない。
			FAnimNode_MmdPhysics* Node =
				NodeProperty->ContainerPtrToValuePtr<FAnimNode_MmdPhysics>(Instance);
			if (Node == nullptr) continue;
			Node->RequestPoseReset();
			Count++;
		}
		return Count;
	}
}

int32 UMmdPhysicsFunctionLibrary::ResetMmdPhysics(USkeletalMeshComponent* Component)
{
	if (Component == nullptr)
	{
		UE_LOG(LogMmdPhysics, Warning,
			TEXT("[MmdPhysics] Reset MMD Physics: コンポーネントが指定されていません。"));
		return 0;
	}

	// 物理は Post-Process Anim Blueprint に入っているのが既定 (【1】がそこへ配線する)。
	// 手で通常の AnimGraph に置いている場合もあるので、両方見る。
	int32 Count = RequestResetOn(Component->GetPostProcessInstance());
	Count += RequestResetOn(Component->GetAnimInstance());

	if (Count == 0)
	{
		UE_LOG(LogMmdPhysics, Warning,
			TEXT("[MmdPhysics] Reset MMD Physics: '%s' に MMD 物理ノードがありません")
			TEXT(" (【1】物理を配線 を実行していない可能性があります)。"),
			*Component->GetName());
	}
	return Count;
}
