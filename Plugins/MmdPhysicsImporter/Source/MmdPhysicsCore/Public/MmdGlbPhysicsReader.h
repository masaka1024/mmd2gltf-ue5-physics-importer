// Copyright (c) 2026 masaka1024. MIT License.
// ===========================================================================
// GLB (glTF binary) の extras.mmd から PmxPhysicsModel を構築するリーダ。
// PmxReader (PMX 直読み) と並ぶ入力経路。UE での通常運用はこちら。
//   - 剛体 : extras.mmd.rigidBodies (PMX raw) をそのままマップ
//   - Joint: extras.mmd.joints (PMX raw) をそのままマップ
//   - ボーン: glTF nodes から 名前 / 親(children階層) / 変形階層(extras.mmd.layer) /
//            raw PMX world 位置(ローカル並進をunitScaleで戻して階層合成)
//   - unitScale は extras.mmd.unitScale から読む (既定を仮定しない)
//   - physicsGltf(変換済みビュー)は使わず raw を使う
// 当面 GLB(バイナリ)のみ対応 (.gltf JSON+外部bin は未対応)。読み取り専用・本体物理に非関与。
//
// ★移植上の相違点: 移植元はコンテナ不正時に InvalidDataException を投げていたが、
//   UE は既定で C++ 例外が無効なので、警告を積んで nullptr を返す形に置き換えている。
//   extras.mmd の中身に対する「例外を投げず読み進める」方針は移植元のまま。
//
// 移植元: Assets/MMD_Scripts/MmdPhysics/Pmx/GlbPhysicsReader.cs (namespace BulletPhysics.Pmx)
// ===========================================================================

#pragma once

#include "MmdPmxPhysicsData.h"
#include "MmdMiniJson.h"

namespace MmdPhysics
{
	class MMDPHYSICSCORE_API GlbPhysicsReader
	{
	public:
		// 座標: extras.mmd は raw PMX (MMD左手系, 未スケール)。glTF は cpos=(x,y,-z)*s。
		// よって glTF ローカル並進 lt から raw ローカルへ戻すのは (lt.x/s, lt.y/s, -lt.z/s)。
		// (バインドは回転恒等なので world = ローカルの単純合成でよい。)

		static TSharedPtr<PmxPhysicsModel> LoadFile(const FString& Path);
		static TSharedPtr<PmxPhysicsModel> LoadFile(const FString& Path, float& OutUnitScale);
		static TSharedPtr<PmxPhysicsModel> LoadFile(const FString& Path, float& OutUnitScale, TArray<FString>& OutWarnings);

		// バイト列(GLB)から直接読む (テスト・メモリ入力用)。
		static TSharedPtr<PmxPhysicsModel> LoadBytes(const TArray<uint8>& Glb, float& OutUnitScale, TArray<FString>& OutWarnings);

		// ibm(inverseBindMatrices)から算出した raw PMX ボーン world 位置 (検証の別経路)。
		static TArray<Vec3> BonePositionsFromIbm(const FString& Path);

		// ---- GLB コンテナ分解 (JSON チャンク + BIN チャンク) ----
		// マテリアル読み取り (GlbMaterialReader) からも使うので公開している。
		static bool ParseGlb(const TArray<uint8>& d, TSharedPtr<MmdJsonValue>& OutRoot, TArray<uint8>& OutBin, TArray<FString>& OutWarnings);

		// accessor(FLOAT)を BIN から読み出す (bufferView.byteOffset + accessor.byteOffset)。
		// モーフアニメーションの読み取り (MmdMorphAnimation) からも使うので公開している。
		// ※componentType は見ない (呼び出し側で FLOAT=5126 を確かめること)。
		//   bufferView.byteStride も見ない = 詰めて並んでいる前提。
		//   物理・アニメーションのアクセサはどちらも詰めて出力される。
		static TArray<float> ReadFloatAccessor(const TSharedPtr<MmdJsonValue>& Root, const TArray<uint8>& Bin, int32 AccIdx);

	private:

		// ---- extras.mmd → PmxPhysicsModel (防御的: 欠損/範囲外でも読み進める) ----
		static TSharedPtr<PmxPhysicsModel> BuildModel(const TSharedPtr<MmdJsonValue>& Root, float& OutUnitScale, TArray<FString>& OutWarnings);

		static Vec3 Vec3FromArr(const TSharedPtr<MmdJsonValue>& o);

		// 移植元のローカル再帰関数 World(i) に対応。
		// ※移植元同様サイクル検出は持たない (glTF の node 階層は木である前提)。
		static Vec3 ComputeWorld(int32 i, int32 nb, const TArray<int32>& Parent, const TArray<Vec3>& LocalRaw,
			TArray<TOptional<Vec3>>& World);
	};
}
