// Copyright (c) 2026 masaka1024. MIT License.
//
// 座標変換 (FMmdUeSpace) の検証。移植で最も事故りやすい箇所なので、
// 「期待する具体値」と「位置と回転が同一の回転であること」を固定する。

#include "Misc/AutomationTest.h"
#include "MmdUeSpace.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace MmdPhysics;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMmdUeSpaceTest, "MmdPhysics.Bridge.UeSpace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMmdUeSpaceTest::RunTest(const FString& Parameters)
{
	const float UnitScale = 0.08f;   // extras.mmd の既定。PMX 1 単位 = 8cm。

	// --- 位置: UE = (x, -z, y) * UnitScale * 100 ---
	{
		const FVector Got = FMmdUeSpace::ToUe(Vec3(1.0f, 2.0f, 3.0f), UnitScale);
		TestTrue(TEXT("位置が (x,-z,y)×8 になる"), Got.Equals(FVector(8.0, -24.0, 16.0), 1e-4));
	}

	// PMX の +Y (上) は UE の +Z (上) へ。
	{
		const FVector Up = FMmdUeSpace::ToUe(Vec3(0, 1, 0), UnitScale);
		TestTrue(TEXT("PMX +Y (上) が UE +Z (上) になる"), Up.Equals(FVector(0, 0, 8), 1e-4));
	}

	// --- 往復 ---
	{
		const Vec3 V(1.5f, -2.25f, 3.75f);
		const Vec3 Back = FMmdUeSpace::ToMmd(FMmdUeSpace::ToUe(V, UnitScale), UnitScale);
		TestTrue(TEXT("位置が往復する"), (Back - V).Length() < 1e-5f);
	}
	{
		const Quat Q = Quat::FromAxisAngle(Vec3(0.3f, -0.5f, 0.8f).Normalized(), 1.1f);
		const Quat Back = FMmdUeSpace::ToMmd(FMmdUeSpace::ToUe(Q));
		TestTrue(TEXT("回転が往復する"),
			FMath::Abs(Back.x - Q.x) < 1e-5f && FMath::Abs(Back.y - Q.y) < 1e-5f &&
			FMath::Abs(Back.z - Q.z) < 1e-5f && FMath::Abs(Back.w - Q.w) < 1e-5f);
	}

	// --- ★核心: 位置と回転が同じ「純粋な回転」であること ---
	//
	// 変換 R が鏡映を含まない純回転なら、
	//     ToUe(q · v) == ToUe(q) · ToUe(v)
	// が成り立つ (ToUe(v)=R·v·S, ToUe(q)=R·q·R⁻¹ なので R q R⁻¹ · R v S = R (q v) S)。
	// もし片方だけ符号を落としていたり鏡映が混ざっていたりすると、ここで必ず破綻する。
	// これが破綻しない限り、MMD のジョイント回転制限は符号反転なしでそのまま渡してよい。
	{
		const Quat Q = Quat::FromAxisAngle(Vec3(0.2f, 0.9f, -0.4f).Normalized(), 0.77f);
		const Vec3 V(2.0f, -1.0f, 0.5f);

		const FVector Lhs = FMmdUeSpace::ToUe(Q * V, UnitScale);
		const FVector Rhs = FMmdUeSpace::ToUe(Q).RotateVector(FMmdUeSpace::ToUe(V, UnitScale));

		TestTrue(FString::Printf(TEXT("位置と回転が同一の純回転 (差 %.6g)"), FVector::Dist(Lhs, Rhs)),
			Lhs.Equals(Rhs, 1e-3));
	}

	// --- 変換行列の行列式が +1 (鏡映でない) ことを直接確かめる ---
	{
		const FVector Ex = FMmdUeSpace::ToUe(Vec3(1, 0, 0), 1.0f / FMmdUeSpace::CmPerMeter);
		const FVector Ey = FMmdUeSpace::ToUe(Vec3(0, 1, 0), 1.0f / FMmdUeSpace::CmPerMeter);
		const FVector Ez = FMmdUeSpace::ToUe(Vec3(0, 0, 1), 1.0f / FMmdUeSpace::CmPerMeter);
		const double Det = FVector::DotProduct(Ex, FVector::CrossProduct(Ey, Ez));
		TestTrue(FString::Printf(TEXT("行列式が +1 (鏡映ではない): det=%.6f"), Det), FMath::IsNearlyEqual(Det, 1.0, 1e-4));
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
