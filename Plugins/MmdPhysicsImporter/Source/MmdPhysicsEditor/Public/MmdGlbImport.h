// Copyright (c) 2026 masaka1024. MIT License.
// ===========================================================================
// 【0】.glb の取り込み。インポーターウィンドウの [0] ボタンと
// コンソールコマンド MmdPhysics.ImportPipeline が同じ処理を通るよう、ここへ切り出してある。
//
//   1. 取り込み先 /Game/<ファイル名>/ の事前確認 (既存アセット・旧形式スケルトン)
//   2. 全角数字を半角へ直した複製を作る (MmdGlbNameNormalize。衝突は振り分け)
//   3. 複製を UAssetImportTask で取り込む (Interchange)
//   4. インポート元を原本へ差し戻す
//
// ★コンソールコマンド (エディタ内 / -ExecCmds で使う):
//     MmdPhysics.ImportPipeline <.glb の絶対パス> [-Force]
//   【0】取り込み →【1】物理を配線 →【2】マテリアル変換 →【3】アクター生成 を順に行い、
//   **このパイプラインが作成・変更したパッケージだけ**を保存する。
//   取り込み先に既存アセットがあれば既定では中止し、-Force のときだけ上書きする。
// ===========================================================================

#pragma once

#include "CoreMinimal.h"

class USkeletalMesh;

/** 取り込み先の事前確認の結果。 */
struct MMDPHYSICSEDITOR_API FMmdImportPrecheck
{
	enum class EState : uint8
	{
		/** 取り込み先にアセットが無い。そのまま取り込める。 */
		Clear,
		/** 既存アセットがある。上書きしてよいか確認が要る。 */
		Existing,
		/** 全角数字のボーン名を持つ旧形式のスケルトンがある。上書きでは直らないので中止する。 */
		Legacy,
	};

	EState State = EState::Clear;

	/** 調べたフォルダ (/Game/<ファイル名>)。 */
	FString Folder;

	/** 既存アセットのオブジェクトパス。 */
	TArray<FString> ExistingAssets;

	/** 旧形式と判定したスケルトンと、その全角数字を含むボーン名 (先頭のいくつか)。 */
	FString LegacySkeleton;
	TArray<FString> LegacyBones;

	FString Message;
};

struct MMDPHYSICSEDITOR_API FMmdGlbImportResult
{
	enum class EFailure : uint8 { None, NotFound, ExistingAssets, LegacyAssets, Normalize, NoMesh };

	bool bSuccess = false;
	EFailure Failure = EFailure::None;

	USkeletalMesh* Mesh = nullptr;

	/** 取り込み元として記録した原本のパス。 */
	FString SourcePath;

	/** 取り込み前の事前確認。 */
	FMmdImportPrecheck Precheck;

	/** 半角化の結果 (件数と、振り分けた衝突の説明)。 */
	FString NormalizeMessage;
	TArray<FString> Collisions;

	/** 取り込みで作成・置き換えられたオブジェクトのパッケージ名。 */
	TArray<FString> ImportedPackages;

	FString Message;
};

struct MMDPHYSICSEDITOR_API FMmdImportPipelineResult
{
	bool bSuccess = false;
	FString Message;

	/** 保存したパッケージ (各段が自分で保存したものと、最後にまとめて保存したものの両方)。 */
	TArray<FString> SavedPackages;

	/**
	 * パイプラインの前から未保存だったため、最後の一括保存から外したパッケージのうち
	 * 取り込み先フォルダにあるもの (パイプラインが変更した可能性がある)。
	 */
	TArray<FString> SkippedPreDirty;
};

class MMDPHYSICSEDITOR_API FMmdGlbImport
{
public:
	/** 取り込み先フォルダ: /Game/<ファイル名> (glTF 既定パイプラインがここへ振り分ける)。 */
	static FString DestinationFolderFor(const FString& GlbPath);

	/** 取り込み先の事前確認。 */
	static FMmdImportPrecheck Precheck(const FString& GlbPath);
	static FMmdImportPrecheck PrecheckFolder(const FString& Folder);

	/**
	 * .glb を /Game へ取り込む (glTF 既定パイプラインが /Game/<ファイル名>/<種別>/ に振り分ける)。
	 * 旧形式のスケルトンがあれば常に中止する。既存アセットがあれば bAllowOverwrite のときだけ上書きする。
	 * 保存はしない。
	 */
	static FMmdGlbImportResult Import(const FString& GlbPath, bool bAllowOverwrite);

	/** 【0】→【3】を行い、作成・変更したパッケージだけを保存する。 */
	static FMmdImportPipelineResult RunPipeline(const FString& GlbPath, bool bForce);
};
