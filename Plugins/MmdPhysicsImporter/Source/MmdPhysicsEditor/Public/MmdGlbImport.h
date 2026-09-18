// Copyright (c) 2026 masaka1024. MIT License.
// ===========================================================================
// 【0】.glb の取り込み。インポーターウィンドウの [0] ボタンと
// コンソールコマンド MmdPhysics.ImportPipeline が同じ処理を通るよう、ここへ切り出してある。
//
//   1. 全角数字を半角へ直した複製を作る (MmdGlbNameNormalize。衝突は振り分け)
//   2. 複製を UAssetImportTask で取り込む (Interchange)
//   3. インポート元を原本へ差し戻す
//
// ★コンソールコマンド (エディタ内 / -ExecCmds で使う):
//     MmdPhysics.ImportPipeline <.glb の絶対パス>
//   【0】取り込み →【1】物理を配線 →【2】マテリアル変換 →【3】アクター生成 を順に行い、
//   変更したパッケージを保存する。ウィンドウのボタンを 0→3 の順に押すのと同じ。
// ===========================================================================

#pragma once

#include "CoreMinimal.h"

class USkeletalMesh;

struct MMDPHYSICSEDITOR_API FMmdGlbImportResult
{
	enum class EFailure : uint8 { None, NotFound, Normalize, NoMesh };

	bool bSuccess = false;
	EFailure Failure = EFailure::None;

	USkeletalMesh* Mesh = nullptr;

	/** 取り込み元として記録した原本のパス。 */
	FString SourcePath;

	/** 半角化の結果 (件数と、振り分けた衝突の説明)。 */
	FString NormalizeMessage;
	TArray<FString> Collisions;

	FString Message;
};

class MMDPHYSICSEDITOR_API FMmdGlbImport
{
public:
	/**
	 * .glb を /Game へ取り込む (glTF 既定パイプラインが /Game/<ファイル名>/<種別>/ に振り分ける)。
	 * 既存のアセットは置き換える (保存はしない)。
	 */
	static FMmdGlbImportResult Import(const FString& GlbPath);
};
