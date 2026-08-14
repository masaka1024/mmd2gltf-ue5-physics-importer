// Copyright (c) 2026 masaka1024. MIT License.
// AnimGraph 上での見た目とピン定義だけを持つエディタ表現。実体は
// MmdPhysicsRuntime の FAnimNode_MmdPhysics 側にある。

#pragma once

#include "CoreMinimal.h"
#include "AnimGraphNode_SkeletalControlBase.h"
#include "AnimNode_MmdPhysics.h"
#include "AnimGraphNode_MmdPhysics.generated.h"

UCLASS(MinimalAPI)
class UAnimGraphNode_MmdPhysics : public UAnimGraphNode_SkeletalControlBase
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = Settings)
	FAnimNode_MmdPhysics Node;

	// UEdGraphNode
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;
	virtual FString GetNodeCategory() const override;

protected:
	// UAnimGraphNode_SkeletalControlBase
	virtual const FAnimNode_SkeletalControlBase* GetNode() const override { return &Node; }
	virtual FText GetControllerDescription() const override;
};
