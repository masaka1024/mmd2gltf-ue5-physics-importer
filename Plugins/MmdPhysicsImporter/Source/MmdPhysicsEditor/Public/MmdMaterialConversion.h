// Copyright (c) 2026 masaka1024. MIT License.
// ===========================================================================
// 【2】マテリアルを MMD トゥーンへ変換 — Unity 版の lilToon 変換に対応する。
//
// UE には lilToon 相当が無いため、マスターマテリアル M_MmdToon を
// **プログラムでノードグラフごと生成**し、マテリアルごとにインスタンスを作る。
// (バイナリの .uasset をリポジトリに置かずに済み、UE のバージョン差にも追随しやすい)
//
// シェーディングは Unlit + 自前の N·L トゥーン。MMD のマテリアルは
// KHR_materials_unlit 付きで出力されており、MMD 本体の陰影もライト方向と
// トゥーンランプだけで決まるため、この方が見た目が近い。
// 代わりにシーンのライトには反応しない (ライト方向はマテリアルのパラメータ)。
//
// アウトライン (エッジ) は今回は描画しない。edgeColor / edgeSize は
// マテリアルインスタンスのパラメータとして保存だけしておく。
// ===========================================================================

#pragma once

#include "CoreMinimal.h"

class USkeletalMesh;
class UMaterial;

struct MMDPHYSICSEDITOR_API FMmdMaterialResult
{
	bool bSuccess = false;
	int32 Converted = 0;
	int32 Total = 0;
	FString Message;
};

class MMDPHYSICSEDITOR_API FMmdMaterialConversion
{
public:
	/**
	 * スケルタルメッシュのマテリアルを MMD トゥーンへ変換する。
	 *
	 * @param Mesh     UE 標準の Interchange glTF で取り込んだスケルタルメッシュ
	 * @param GlbPath  mmd2gltf-gui が出力した .glb (各マテリアルの extras.mmd を読む)
	 */
	static FMmdMaterialResult ConvertMaterials(USkeletalMesh* Mesh, const FString& GlbPath);

	/**
	 * マスターマテリアル M_MmdToon を用意する (無ければ生成する)。
	 * @param PackagePath 生成先 (例 "/Game/IA")
	 */
	static UMaterial* EnsureMasterMaterial(const FString& PackagePath);
};
