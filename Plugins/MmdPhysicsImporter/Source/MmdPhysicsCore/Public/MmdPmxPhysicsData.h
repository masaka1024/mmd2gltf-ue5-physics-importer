// Copyright (c) 2026 masaka1024. MIT License.
// ===========================================================================
// Bullet 互換物理エンジン – PMX 物理データ構造
// PMX ファイルから抽出した剛体 / Joint / SoftBody の生パラメータ。
//
// ★ここに入る値はすべて PMX ネイティブの生値であり、単位変換も座標変換も掛けない。
//   UE 座標系への変換は MmdPhysicsRuntime の FMmdUeSpace が唯一の担当。
//
// 移植元: Assets/MMD_Scripts/MmdPhysics/Pmx/PmxPhysicsData.cs (namespace BulletPhysics.Pmx)
// ===========================================================================

#pragma once

#include "MmdMathTypes.h"

namespace MmdPhysics
{
	/** PMX 剛体レコード (仕様 ●剛体)。 */
	struct PmxRigidBody
	{
		FString Name;
		FString NameEn;
		int32 BoneIndex = -1;
		uint8 Group = 0;
		uint16 NonCollisionGroup = 0;
		uint8 ShapeType = 0;       // 0:球 1:箱 2:カプセル
		Vec3 Size;                 // 半径/半径長
		Vec3 Position;             // ワールド位置
		Vec3 Rotation;             // ラジアン
		float Mass = 0.0f;
		float LinearDamping = 0.0f;
		float AngularDamping = 0.0f;
		float Restitution = 0.0f;
		float Friction = 0.0f;
		uint8 PhysicsMode = 0;     // 0:追従 1:物理 2:物理+Bone合わせ
	};

	/** PMX Joint レコード (仕様 ●Joint)。 */
	struct PmxJoint
	{
		FString Name;
		FString NameEn;
		uint8 JointType = 0;       // 0..5
		int32 RigidBodyAIndex = -1;
		int32 RigidBodyBIndex = -1;
		Vec3 Position;
		Vec3 Rotation;             // ラジアン
		Vec3 LinearLowerLimit;
		Vec3 LinearUpperLimit;
		Vec3 AngularLowerLimit;
		Vec3 AngularUpperLimit;
		Vec3 SpringLinear;
		Vec3 SpringAngular;
	};

	/** PMX SoftBody アンカー。 */
	struct PmxSoftBodyAnchor
	{
		int32 RigidBodyIndex = 0;
		int32 VertexIndex = 0;
		bool NearMode = false;
	};

	/** PMX SoftBody レコード (仕様 ●SoftBody 2.1)。 */
	struct PmxSoftBody
	{
		FString Name;
		FString NameEn;
		uint8 Shape = 0;           // 0:TriMesh 1:Rope
		int32 MaterialIndex = -1;
		uint8 Group = 0;
		uint16 NonCollisionGroup = 0;
		uint8 Flags = 0;           // 0x01:B-Link 0x02:Cluster 0x04:LinkCross
		int32 BLinkDistance = 0;
		int32 ClusterCount = 0;
		float TotalMass = 0.0f;
		float CollisionMargin = 0.0f;
		int32 AeroModel = 0;

		// config
		float VCF = 0, DP = 0, DG = 0, LF = 0, PR = 0, VC = 0, DF = 0, MT = 0, CHR = 0, KHR = 0, SHR = 0, AHR = 0;
		// cluster
		float SRHR_CL = 0, SKHR_CL = 0, SSHR_CL = 0, SR_SPLT_CL = 0, SK_SPLT_CL = 0, SS_SPLT_CL = 0;
		// iteration
		int32 V_IT = 0, P_IT = 0, D_IT = 0, C_IT = 0;
		// material
		float LST = 0, AST = 0, VST = 0;

		TArray<PmxSoftBodyAnchor> Anchors;
		TArray<int32> PinVertices;
	};

	/** PMX から抽出した物理関連データ一式。 */
	struct PmxPhysicsModel
	{
		FString ModelName;
		FString ModelNameEn;
		float Version = 0.0f;

		TArray<PmxRigidBody> RigidBodies;
		TArray<PmxJoint> Joints;
		TArray<PmxSoftBody> SoftBodies;

		// ボーン名 (剛体<->ボーン紐付け用に最小限保持)。BoneNames と同じ添字で引ける。
		TArray<FString> BoneNames;
		TArray<Vec3> BonePositions;
		TArray<int32> BoneParents;       // 親ボーンindex (-1 = ルート)
		TArray<int32> BoneDeformLayers;  // 変形階層 (FK計算順の参考)
	};
}
