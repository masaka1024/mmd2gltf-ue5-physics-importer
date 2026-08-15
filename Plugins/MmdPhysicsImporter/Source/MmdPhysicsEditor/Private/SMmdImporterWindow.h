// Copyright (c) 2026 masaka1024. MIT License.
// インポーターウィンドウ。移植元 Unity 版 MmdPhysicsImporterWindow に対応する。
// 日英切替も移植元にならって持つ (移植元の L(ja, en) ヘルパ相当)。

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class USkeletalMesh;

class SMmdImporterWindow : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMmdImporterWindow) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	static const FName TabId;

private:
	/** 移植元の L(ja, en) 相当。 */
	FText L(const FString& Ja, const FString& En) const { return FText::FromString(bUseEnglish ? En : Ja); }

	FString GetMeshPath() const;
	void OnMeshChanged(const FAssetData& AssetData);

	FReply OnBrowseGlb();
	FReply OnWirePhysics();
	FReply OnConvertMaterials();
	FReply OnBuildActor();
	bool CanWire() const;

	TWeakObjectPtr<USkeletalMesh> TargetMesh;
	FString GlbPath;
	FString StatusText;
	bool bStatusIsError = false;
	bool bUseEnglish = false;
};
