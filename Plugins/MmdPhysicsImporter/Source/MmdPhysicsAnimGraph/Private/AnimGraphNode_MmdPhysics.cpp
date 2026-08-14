// Copyright (c) 2026 masaka1024. MIT License.

#include "AnimGraphNode_MmdPhysics.h"

#define LOCTEXT_NAMESPACE "AnimGraphNode_MmdPhysics"

FText UAnimGraphNode_MmdPhysics::GetControllerDescription() const
{
	return LOCTEXT("ControllerDescription", "MMD Physics");
}

FText UAnimGraphNode_MmdPhysics::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return GetControllerDescription();
}

FText UAnimGraphNode_MmdPhysics::GetTooltipText() const
{
	return LOCTEXT("Tooltip",
		"mmd2gltf-gui が出力した .glb の extras.mmd から MMD の剛体・ジョイントを読み、"
		"自作 Bullet 互換エンジンで揺れ物ボーンを駆動します。"
		"スケルタルメッシュの Post-Process Anim Blueprint に置いてください。");
}

FString UAnimGraphNode_MmdPhysics::GetNodeCategory() const
{
	return TEXT("MMD");
}

#undef LOCTEXT_NAMESPACE
