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

	/**
	 * TargetMesh のインポート元 .glb を AssetImportData から拾って GlbPath に入れる。
	 * 見つからなければ空にする (前のメッシュのパスが残って別モデルに当たるのを防ぐ)。
	 */
	void AutoFillGlbPathFromMesh();

	FReply OnBrowseGlb();

	/**
	 * 【0】.glb を取り込む。
	 *
	 * ★UE へ直接 D&D せずここから取り込む理由。
	 *   PMX の指ボーン `右人指１` などは名前に全角数字を含み、そのままだと
	 *   URigHierarchy::SanitizeName で `右人指_` に潰れて衝突し、衝突した分の
	 *   アニメーショントラックが取り込みで捨てられる (IA で 54 本中 18 本が脱落)。
	 *   全角数字は iswalpha も iswdigit も通らず、ロケールでもエンジン側の
	 *   判定式でも救えない (MmdNameNormalize.h)。
	 *   そこで**取り込む直前に半角へ直した複製を作り、それを食わせる**。
	 *   原本 .glb は書き換えないし、エクスポーターも直さない。
	 */
	FReply OnImportGlb();
	bool CanImport() const;

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
