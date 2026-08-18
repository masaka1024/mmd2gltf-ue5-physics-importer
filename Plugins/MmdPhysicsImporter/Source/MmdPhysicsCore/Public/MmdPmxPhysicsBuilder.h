// Copyright (c) 2026 masaka1024. MIT License.
// ===========================================================================
// Bullet 互換物理エンジン – PMX -> PhysicsWorld ビルダー
// PMX の剛体/Joint を物理エンジンのインスタンスへ変換する。
// 剛体<->ボーンのオフセット (バインドポーズ) も生成する。
//
// 移植元: Assets/MMD_Scripts/MmdPhysics/Pmx/PmxPhysicsBuilder.cs (namespace BulletPhysics.Pmx)
// ===========================================================================

#pragma once

#include "MmdPhysicsWorld.h"
#include "MmdPmxPhysicsData.h"

namespace MmdPhysics
{
	/** 剛体とボーンの紐付け。ボーン追従 / 物理フィードバックに使う。 */
	struct BoneLink
	{
		RigidBody* Body = nullptr;
		int32 BoneIndex = -1;
		// バインド時の「ボーン→剛体」相対変換 (bone^-1 * body)。
		RigidTransform BodyOffsetFromBone;
		EPhysicsMode Mode = EPhysicsMode::Dynamic;
	};

	/**
	 * ボーン姿勢の問い合わせ。移植元 C# の System.Func<int, RigidTransform?> に対応。
	 * 未設定 (バインドのまま / 姿勢が得られない) は未設定の TOptional を返す。
	 */
	using FBoneWorldGetter = TFunctionRef<TOptional<RigidTransform>(int32)>;

	class MMDPHYSICSCORE_API PmxPhysicsBuilder
	{
	public:
		PhysicsWorld World;
		TArray<BoneLink> BoneLinks;
		// World.Bodies が所有する実体への参照 (PMX 剛体 index と同じ並び)。
		TArray<RigidBody*> Bodies;

		static TSharedPtr<PmxPhysicsBuilder> Build(const TSharedPtr<PmxPhysicsModel>& Model);

		/**
		 * 物理開始/リセット時に、動的剛体も含む全剛体を現在のボーン姿勢へ整合させる
		 * (MMD の物理演算リセット相当)。剛体を boneWorld * BodyOffsetFromBone に置き、
		 * 速度を 0、慣性ワールドを更新、接触/蓄積インパルスをクリアする。
		 * これをしないと、フレーム0で脚が曲がっている場合に kinematic な脚コライダーだけが
		 * フレーム0へ動き、動的スカートがバインド位置に取り残されて逃げられない貫入平衡に落ちる。
		 *
		 * GetBoneWorld: ボーンindex → そのボーンのワールド姿勢 (無ければ未設定)。
		 *   未設定を返したボーン (BoneIndex<0 や、姿勢が得られないボーン) はバインド位置のままとする。
		 */
		void ResetBodiesToBonePose(FBoneWorldGetter GetBoneWorld);

		/**
		 * FK-rest 物理リセット。剛体を「ボーンの FK-rest ワールド姿勢 * BodyOffsetFromBone」へ置く。
		 * FK-rest = 親駆動のバインド整合姿勢:
		 *   - 物理で動くボーン (動的剛体が紐づくボーン) は、外から与えられた姿勢を使わず、
		 *     親チェーンから前計算する (バインドは位置のみ・回転恒等なので単純な階層合成)。
		 *   - 駆動されるボーン (kinematic 剛体のボーン等) は GetDrivenBoneWorld の姿勢を使う。
		 * これにより、MMD の物理結果(傾き込み)を全剛体へ一斉適用したときの過拘束発散を避け、
		 * 各経路で同一の開始状態を作れる。
		 * GetDrivenBoneWorld: ボーンindex → 駆動姿勢 (無ければ未設定)。物理ボーンには使われない。
		 */
		void ResetBodiesToBonePoseFk(FBoneWorldGetter GetDrivenBoneWorld);

		/**
		 * 駆動(BoneFollow)剛体の KinematicTarget を「ボーンworld姿勢 * BodyOffsetFromBone」で設定する共通ヘルパ。
		 * ★駆動式は必ずこの1箇所を使うこと。呼び出し側で手書きしない。
		 * (2026-08-09 事故: 手書きで `= bw`(offset欠落)とし、体コライダーが誤配置のまま
		 *  全simが走って貫入系の数字が全て汚染された。再発防止のため集約+回帰テストあり)
		 * GetDrivenBoneWorld が未設定を返したボーンは前回ターゲット維持 (テレポートしない)。
		 */
		void ApplyKinematicTargets(FBoneWorldGetter GetDrivenBoneWorld);

		/**
		 * mode2 (DynamicBoneMerge) の剛体が1つでもあるか。
		 * 書き戻し時に補正姿勢の計算が必要かの判定に使う (無ければ計算ごと省ける)。
		 */
		bool HasBoneMergeBodies() const { return _hasBoneMerge; }

		/**
		 * [物理+ボーン位置合わせ] 再現 (PmxEditorの補正層。補正OFF/ON対照データで式を確定, 2026-08-09):
		 *   物理ボーンの出力姿勢 = 位置: 親ボーン(補正済)の位置 + 親回転で回した bind オフセット
		 *                          (物理の「移動分」を捨てる) / 回転: 物理回転そのまま。
		 * 検証: |ON子-(ON親+qON親·bindRel)| = skirt中央0.011 (MMD(補正ON)でほぼ厳密成立, OFFは0.072)。
		 * 駆動ボーンは GetDrivenBoneWorld、物理ボーンの回転は剛体から復元 (body * offset^-1)。
		 * 書き戻しは必ず本ヘルパを共用する (FK-rest リセットと同じ扱い。経路差のバグを避ける)。
		 * 戻り値: boneIndex -> 補正済world姿勢。
		 *
		 * @param GetDrivenBoneWorld 駆動ボーンの現在world姿勢 (未設定=バインド)。
		 * @param RotClampAlpha      回転clamp割合 (0=無効=回転そのまま / 1=リミットへ完全clamp)。
		 *   [Jointロック内部演算] 再現の第一形: 親側ジョイントの相対euler(補正済親フレーム基準)を
		 *   リミット超過分だけ α で戻す。MMD(補正ON)の超過8-14°は完全clampでないことを示すため中間αを掃引する。
		 * 順序比較(位置を物理回転で再構成→回転のみclamp)は、呼び出し側で α=0 と α>0 の2回呼びを合成する。
		 * @param bAlignAllPositions すべての物理ボーンの位置を再構成するか。
		 *   false (既定) では **mode2 のボーンだけ**を再構成し、mode1 のボーンは
		 *   剛体の生の姿勢をそのまま返す。書き戻し側 (FAnimNode_MmdPhysics) が
		 *   mode1 を生の姿勢で書くので、ここで bind 長の鎖を作り直すと
		 *   「画面に出ている親」と「mode2 の子が基準にした親」が別物になり、
		 *   鎖の最後の 1 節だけが伸び縮みする (実測: しっぽ１３ が 0.09〜0.65 倍)。
		 *   true は bAlignBonePositions 相当で、鎖全体を一貫して再構成する。
		 */
		TArray<TOptional<RigidTransform>> ComputeAlignedBonePoses(FBoneWorldGetter GetDrivenBoneWorld,
			float RotClampAlpha = 0.0f, bool bAlignAllPositions = false);

	private:
		TSharedPtr<PmxPhysicsModel> _model;   // FK-rest計算用にボーン階層を参照
		bool _hasBoneMerge = false;

		void BuildBodies(const PmxPhysicsModel& Model);
		void BuildJoints(const PmxPhysicsModel& Model);

		RigidBody* ValidBody(int32 Index) const
		{
			return (Index >= 0 && Index < Bodies.Num()) ? Bodies[Index] : nullptr;
		}

		static RigidTransform ComputeOffset(const PmxPhysicsModel& Model, const PmxRigidBody& rb);
		static TSharedPtr<CollisionShape> CreateShape(const PmxRigidBody& rb);
		static Quat EulerQ(const Vec3& e);

		// 移植元のローカル再帰関数 Fk / Align に対応 (C++ ではラムダの自己再帰が書けないためメソッド化)。
		RigidTransform Fk(int32 i, int32 Depth, int32 n, const TArray<bool>& IsPhysics,
			TArray<TOptional<RigidTransform>>& World_, FBoneWorldGetter GetDrivenBoneWorld) const;

		RigidTransform Align(int32 i, int32 Depth, int32 n,
			const TArray<TOptional<Quat>>& PhysRot,
			const TMap<RigidBody*, BoneLink>& LinkOf,
			// ボーンindex -> そのボーンの BoneLink (無ければ null)。mode1/mode2 の判定に使う。
			const TArray<const BoneLink*>& LinkOfBone,
			const TMap<int32, Joint*>* ParentJoint,
			float RotClampAlpha, bool bAlignAllPositions,
			TArray<TOptional<RigidTransform>>& World_,
			FBoneWorldGetter GetDrivenBoneWorld) const;
	};
}
