// Copyright (c) 2026 masaka1024. MIT License.
//
// 移植の健全性テスト。実モデル (mmd2gltf-gui 出力の .glb) が無くても走る合成データで、
// 「数学型 → 形状 → 剛体 → ジョイント → ソルバ → 積分」が一通り機能することを確認する。
//
// ★これは検証A (C# 版との数値パリティ) の代わりではない。パリティは実モデルの GLB が
//   手に入ってから MmdPhysicsGlbParityTest 側で取る。ここで見るのは
//   「翻訳の過程で式や符号を落としていないか」という粗い健全性のみ。

#include "Misc/AutomationTest.h"
#include "MmdPmxPhysicsBuilder.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace MmdPhysics;

namespace
{
	/** 数学型そのものの検証で使う許容差。float の丸めを跨ぐ比較のみに使う。 */
	constexpr float KTol = 1e-4f;

	/** アンカー (kinematic 球) の位置。ボーン0 / 剛体0。 */
	const Vec3 KAnchorPos(0, 10, 0);

	/**
	 * 合成モデル: 上端の kinematic 球にぶら下がる 1 本の振り子。
	 * ボーン0 = 固定点 (mode0 = ボーン追従)、ボーン1 = 揺れ物 (mode1 = 物理)。
	 * ジョイントは並進ロック / 回転フリー = 球面ジョイント相当。
	 *
	 * BobPos をアンカーの真下に置くと重力と拘束が釣り合って動かない (それが正しい)。
	 * 振らせたいときは水平にずらす。
	 */
	TSharedPtr<PmxPhysicsModel> MakePendulumModel(const Vec3& BobPos)
	{
		TSharedPtr<PmxPhysicsModel> M = MakeShared<PmxPhysicsModel>();

		M->BoneNames.Add(TEXT("アンカー"));
		M->BonePositions.Add(KAnchorPos);
		M->BoneParents.Add(-1);
		M->BoneDeformLayers.Add(0);

		M->BoneNames.Add(TEXT("揺れ"));
		M->BonePositions.Add(BobPos);
		M->BoneParents.Add(0);
		M->BoneDeformLayers.Add(0);

		PmxRigidBody Anchor;
		Anchor.Name = TEXT("剛体_アンカー");
		Anchor.BoneIndex = 0;
		Anchor.Group = 0;
		// PMX の 16bit は「そのグループと衝突する」ビット。全グループと衝突可にする。
		Anchor.NonCollisionGroup = 0xFFFF;
		Anchor.ShapeType = 0;               // 球
		Anchor.Size = Vec3(0.5f, 0, 0);
		Anchor.Position = KAnchorPos;
		Anchor.Mass = 0.0f;
		Anchor.PhysicsMode = 0;             // ボーン追従 (kinematic)
		M->RigidBodies.Add(Anchor);

		PmxRigidBody Bob;
		Bob.Name = TEXT("剛体_揺れ");
		Bob.BoneIndex = 1;
		Bob.Group = 1;
		Bob.NonCollisionGroup = 0xFFFF;
		Bob.ShapeType = 2;                  // カプセル
		Bob.Size = Vec3(0.3f, 0.6f, 0);
		Bob.Position = BobPos;
		Bob.Mass = 1.0f;
		Bob.LinearDamping = 0.5f;
		Bob.AngularDamping = 0.5f;
		Bob.PhysicsMode = 1;                // 物理演算
		M->RigidBodies.Add(Bob);

		PmxJoint J;
		J.Name = TEXT("J_揺れ");
		J.JointType = 0;                    // バネ付6DOF
		J.RigidBodyAIndex = 0;
		J.RigidBodyBIndex = 1;
		// ジョイントはアンカーと錘の中点。並進ロックなので、ここが回転中心になる。
		J.Position = (KAnchorPos + BobPos) * 0.5f;
		// 並進ロック (lo==hi)。回転は lo>hi = フリー。
		J.LinearLowerLimit = Vec3::Zero;
		J.LinearUpperLimit = Vec3::Zero;
		J.AngularLowerLimit = Vec3(1, 1, 1);
		J.AngularUpperLimit = Vec3(-1, -1, -1);
		M->Joints.Add(J);

		return M;
	}
}

// ---------------------------------------------------------------------------
// 数学型: 移植で最も壊れやすい YXZ オイラーと XYZ 分解の往復。
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMmdPhysicsMathTest, "MmdPhysics.Core.Math",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMmdPhysicsMathTest::RunTest(const FString& Parameters)
{
	// 単位クォータニオンの既定値。移植元は new Quat() が全ゼロ (w=0) になる仕様。
	TestEqual(TEXT("Quat の既定は全ゼロ (w=0)"), Quat().w, 0.0f);
	TestEqual(TEXT("Quat::Identity の w は 1"), Quat::Identity.w, 1.0f);

	// FromEulerYxz は R = Ry * Rx * Rz。単軸なら軸角と一致するはず。
	{
		const float A = 0.7f;
		const Quat ByEuler = Quat::FromEulerYxz(A, 0, 0);
		const Quat ByAxis = Quat::FromAxisAngle(Vec3::XAxis, A);
		TestTrue(TEXT("FromEulerYxz(X) == FromAxisAngle(X)"), ByEuler.Equals(ByAxis));
	}
	{
		const float A = -0.4f;
		const Quat ByEuler = Quat::FromEulerYxz(0, A, 0);
		const Quat ByAxis = Quat::FromAxisAngle(Vec3::YAxis, A);
		TestTrue(TEXT("FromEulerYxz(Y) == FromAxisAngle(Y)"), ByEuler.Equals(ByAxis));
	}

	// ToEulerXYZ は Joint の角度リミット判定の土台。合成→分解で戻ること。
	{
		const Vec3 E(0.21f, -0.33f, 0.12f);
		const Quat Q = Quat::FromAxisAngle(Vec3::XAxis, E.x)
			* Quat::FromAxisAngle(Vec3::YAxis, E.y)
			* Quat::FromAxisAngle(Vec3::ZAxis, E.z);
		const Vec3 Back = Joint::ToEulerXYZ(Q.Normalized());
		TestTrue(TEXT("ToEulerXYZ が XYZ 合成を復元する"),
			FMath::Abs(Back.x - E.x) < KTol && FMath::Abs(Back.y - E.y) < KTol && FMath::Abs(Back.z - E.z) < KTol);
	}

	// RigidTransform の逆変換往復。
	{
		const RigidTransform T(Quat::FromAxisAngle(Vec3(1, 2, 3).Normalized(), 0.9f), Vec3(4, -5, 6));
		const Vec3 P(1.5f, -2.5f, 3.5f);
		const Vec3 Round = T.InverseTransformPoint(T.TransformPoint(P));
		TestTrue(TEXT("RigidTransform の順逆が往復する"), (Round - P).Length() < KTol);
	}

	// カプセル慣性の Bullet 準拠マージン (2026-08-13 の修正が落ちていないこと)。
	{
		TestEqual(TEXT("CapsuleShape::InertiaMargin は 0.04"), CapsuleShape::InertiaMargin, 0.04f);
		const CapsuleShape Cap(0.15f, 0.30f);
		const Vec3 I = Cap.CalculateLocalInertia(1.0f);
		// margin=0 のときの値より必ず大きくなる (過小評価の修正なので)。
		const float Saved = CapsuleShape::InertiaMargin;
		CapsuleShape::InertiaMargin = 0.0f;
		const Vec3 I0 = Cap.CalculateLocalInertia(1.0f);
		CapsuleShape::InertiaMargin = Saved;
		TestTrue(TEXT("マージン込みの慣性は margin=0 より大きい"), I.x > I0.x && I.y > I0.y && I.z > I0.z);
	}

	return true;
}

// ---------------------------------------------------------------------------
// エンジン: 合成した振り子が発散せず、拘束に保持されたまま重力で振れること。
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMmdPhysicsPendulumTest, "MmdPhysics.Core.Pendulum",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMmdPhysicsPendulumTest::RunTest(const FString& Parameters)
{
	// 錘を水平にずらして吊るす。真下だと重力と拘束が釣り合って動かないため、
	// 「重力が効いているか」を見るには初期姿勢に傾きが要る (釣り合い側は下の Equilibrium テスト)。
	TSharedPtr<PmxPhysicsModel> Model = MakePendulumModel(Vec3(1, 9, 0));
	TSharedPtr<PmxPhysicsBuilder> B = PmxPhysicsBuilder::Build(Model);

	TestEqual(TEXT("剛体が 2 体構築される"), B->Bodies.Num(), 2);
	TestEqual(TEXT("ジョイントが 1 本構築される"), B->World.Joints.Num(), 1);
	TestEqual(TEXT("ボーンリンクが 2 件"), B->BoneLinks.Num(), 2);

	// mode0 は質量 0 (kinematic)、mode1 は PMX 質量。
	TestTrue(TEXT("アンカーは kinematic"), B->Bodies[0]->IsKinematic());
	TestTrue(TEXT("揺れ物は動的で逆質量を持つ"), !B->Bodies[1]->IsKinematic() && B->Bodies[1]->InverseMass > 0.0f);

	// 剛体とボーンのバインドオフセット (bone^-1 * body)。ボーン位置と剛体位置が同じなら恒等。
	TestTrue(TEXT("バインドオフセットが恒等"), B->BoneLinks[1].BodyOffsetFromBone.Origin.Length() < KTol);

	const Vec3 Start = B->Bodies[1]->WorldTransform.Origin;

	// アンカーを固定したまま 2 秒ぶん回す。
	const RigidTransform AnchorPose(Quat::Identity, KAnchorPos);
	for (int32 Frame = 0; Frame < 60; Frame++)
	{
		B->ApplyKinematicTargets([&AnchorPose](int32 BoneIndex) -> TOptional<RigidTransform>
		{
			return BoneIndex == 0 ? TOptional<RigidTransform>(AnchorPose) : TOptional<RigidTransform>();
		});
		B->World.StepSimulation(1.0f / 30.0f);
	}

	const Vec3 End = B->Bodies[1]->WorldTransform.Origin;
	const Quat EndRot = B->Bodies[1]->WorldTransform.Rotation;

	// 1) 発散していない (NaN / Inf を出していない)。
	const bool bFinite = FMath::IsFinite(End.x) && FMath::IsFinite(End.y) && FMath::IsFinite(End.z)
		&& FMath::IsFinite(EndRot.x) && FMath::IsFinite(EndRot.y) && FMath::IsFinite(EndRot.z) && FMath::IsFinite(EndRot.w);
	TestTrue(TEXT("姿勢が有限 (発散していない)"), bFinite);

	// 2) 重力が効いている = 何かしら動いた。
	TestTrue(TEXT("重力で初期位置から動いた"), (End - Start).Length() > 1e-3f);

	// 3) ジョイントに保持されている = アンカーから離れて飛んで行っていない。
	//    初期のアンカー距離は |(1,-1,0)| = 1.414。並進ロックなのでほぼ不変のはず。
	const float StartDist = (Start - KAnchorPos).Length();
	const float DistFromAnchor = (End - KAnchorPos).Length();
	TestTrue(FString::Printf(TEXT("並進ロックが距離を保っている (%.3f → %.3f)"), StartDist, DistFromAnchor),
		FMath::Abs(DistFromAnchor - StartDist) < 0.2f);

	// 4) 減衰付きの振り子なので、重力方向 (アンカーの真下) へ寄っている。
	TestTrue(FString::Printf(TEXT("アンカーより下へ垂れた (y=%.3f < %.3f)"), End.y, Start.y), End.y < Start.y);
	TestTrue(FString::Printf(TEXT("真下へ寄った (|x| %.3f → %.3f)"), FMath::Abs(Start.x), FMath::Abs(End.x)),
		FMath::Abs(End.x) < FMath::Abs(Start.x));

	// 5) 衝突ペアが張られている (Group/Mask の解釈が反転していない)。
	//    立っているビットが「衝突する」なので、0xFFFF 同士は 1 ペアになるはず。
	TestEqual(TEXT("衝突候補ペアが 1 組"), B->World.DebugCollisionPairCount(), 1);

	return true;
}

// ---------------------------------------------------------------------------
// 釣り合い: 錘がアンカーの真下なら、重力と並進ロックが釣り合って動かないのが正しい。
// ここが動く場合は Baumgarte が過剰にエネルギーを注いでいるか、拘束が効いていない。
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMmdPhysicsEquilibriumTest, "MmdPhysics.Core.Equilibrium",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMmdPhysicsEquilibriumTest::RunTest(const FString& Parameters)
{
	TSharedPtr<PmxPhysicsModel> Model = MakePendulumModel(Vec3(0, 9, 0)); // アンカーの真下
	TSharedPtr<PmxPhysicsBuilder> B = PmxPhysicsBuilder::Build(Model);

	const Vec3 Start = B->Bodies[1]->WorldTransform.Origin;
	const RigidTransform AnchorPose(Quat::Identity, KAnchorPos);
	for (int32 Frame = 0; Frame < 60; Frame++)
	{
		B->ApplyKinematicTargets([&AnchorPose](int32 BoneIndex) -> TOptional<RigidTransform>
		{
			return BoneIndex == 0 ? TOptional<RigidTransform>(AnchorPose) : TOptional<RigidTransform>();
		});
		B->World.StepSimulation(1.0f / 30.0f);
	}
	const Vec3 End = B->Bodies[1]->WorldTransform.Origin;
	const float Drift = (End - Start).Length();

	TestTrue(FString::Printf(TEXT("真下の釣り合いを保っている (ドリフト %.5f < 0.01)"), Drift), Drift < 0.01f);
	return true;
}

// ---------------------------------------------------------------------------
// 非衝突グループのビット解釈。反転させると揺れ物が毎フレーム押し出されて発散するため、
// 移植で最も静かに壊れる箇所。明示的に固定する。
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMmdPhysicsCollisionMaskTest, "MmdPhysics.Core.CollisionMask",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMmdPhysicsCollisionMaskTest::RunTest(const FString& Parameters)
{
	TSharedPtr<CollisionShape> S = MakeShared<SphereShape>(1.0f);
	RigidBody A(S), Bd(S);
	A.Group = 0; Bd.Group = 1;

	// bit=1 が「そのグループと衝突する」。互いのグループ bit を立てれば衝突する。
	A.CollisionMask = 1 << 1;
	Bd.CollisionMask = 1 << 0;
	TestTrue(TEXT("互いのグループ bit が立っていれば衝突する"), PhysicsWorld::ShouldCollide(&A, &Bd));

	// 片側でも落とせば衝突しない (双方向判定)。
	Bd.CollisionMask = 0;
	TestFalse(TEXT("片側の bit が落ちていれば衝突しない"), PhysicsWorld::ShouldCollide(&A, &Bd));

	return true;
}

// ---------------------------------------------------------------------------
// 固定刻みアキュムレータの積み残し。
//
// シェーダーコンパイルやアセットロードで数秒止まると、その時間がまるごと積まれる。
// 上限を掛けずに持ち越すと、以後しばらく毎フレーム上限いっぱいのステップ
// (= 実時間の 8 倍速) で回り続け、髪とスカートが暴れて体に潜り込む。
// 落ちた時間を取り戻す価値は無いので、上限を超えた分は捨てる。
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMmdPhysicsAccumulatorTest, "MmdPhysics.Core.Accumulator",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMmdPhysicsAccumulatorTest::RunTest(const FString& Parameters)
{
	PhysicsWorld World;
	World.FixedTimeStep = 1.0f / 60.0f;

	// 通常のフレーム: 1 ステップずつ進み、何も捨てない。
	World.StepSimulation(1.0f / 60.0f);
	TestEqual(TEXT("通常フレームは 1 ステップ"), World.LastStepsRun, 1);
	TestEqual(TEXT("通常フレームでは何も捨てない"), World.DiscardedTime, 0.0f);

	// 5 秒のハング。上限までしか走らず、超過分は捨てる。
	World.StepSimulation(5.0f);
	TestEqual(TEXT("1 回の呼び出しは上限ステップまで"),
		World.LastStepsRun, PhysicsWorld::MaxStepsPerCall);
	TestTrue(FString::Printf(TEXT("超過分を捨てている (%.3f 秒)"), World.DiscardedTime),
		World.DiscardedTime > 4.8f);

	// ★ここが本題。次のフレームは借金を引きずらず、実時間へ戻っていること。
	World.StepSimulation(1.0f / 60.0f);
	TestEqual(TEXT("次のフレームは 1 ステップに戻る (借金を持ち越さない)"), World.LastStepsRun, 1);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
