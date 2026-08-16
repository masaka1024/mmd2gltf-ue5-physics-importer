// Copyright (c) 2026 masaka1024. MIT License.
//
// 検証: 静止区間で揺れ物が収まるか。
//
// ダンスの開始前など、体がほとんど動かない区間で髪とスカートが揺れ続けると目立つ。
// ここでは**ボーンを一切動かさず**にステップだけ回し、剛体の残留振動を測る。
// 入力が完全に静止しているので、揺れが残るならそれは物理側が出しているもの。
//
// ★刻みは **ノード側の既定 (1/60 × 2)** に合わせる。パリティテストはコア既定
//   (1/30 × 4) で回すので、実際に再生される設定での挙動はそちらでは見ていない。
//
//   MMD_PARITY_GLB … 測定に使う .glb (パリティテストと同じものを使う)
//   MMD_IDLE_SECONDS … 測定時間 (既定 20 秒)

#include "Misc/AutomationTest.h"
#include "HAL/PlatformMisc.h"
#include "MmdGlbPhysicsReader.h"
#include "MmdPmxPhysicsBuilder.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace MmdPhysics;

namespace
{
	/** 1 つの剛体の、ある区間での振れ幅 (各軸の最大-最小のうち最大)。 */
	struct FIdleTrack
	{
		Vec3 Min = Vec3(FLT_MAX, FLT_MAX, FLT_MAX);
		Vec3 Max = Vec3(-FLT_MAX, -FLT_MAX, -FLT_MAX);

		void Add(const Vec3& P)
		{
			Min = Vec3(FMath::Min(Min.x, P.x), FMath::Min(Min.y, P.y), FMath::Min(Min.z, P.z));
			Max = Vec3(FMath::Max(Max.x, P.x), FMath::Max(Max.y, P.y), FMath::Max(Max.z, P.z));
		}
		float PeakToPeak() const
		{
			if (Min.x > Max.x) return 0.0f;
			return FMath::Max3(Max.x - Min.x, Max.y - Min.y, Max.z - Min.z);
		}
		void Reset() { *this = FIdleTrack(); }
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMmdIdleSettleTest, "MmdPhysics.Core.IdleSettle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMmdIdleSettleTest::RunTest(const FString& Parameters)
{
	const FString GlbPath = FPlatformMisc::GetEnvironmentVariable(TEXT("MMD_PARITY_GLB"));
	if (GlbPath.IsEmpty())
	{
		AddInfo(TEXT("MMD_PARITY_GLB が未設定のためスキップ (データ非同梱のため既定でスキップ)。"));
		return true;
	}

	const FString SecEnv = FPlatformMisc::GetEnvironmentVariable(TEXT("MMD_IDLE_SECONDS"));
	const float Seconds = SecEnv.IsEmpty() ? 20.0f : FCString::Atof(*SecEnv);

	float UnitScale = 0.0f;
	TArray<FString> Warnings;
	TSharedPtr<PmxPhysicsModel> Model = GlbPhysicsReader::LoadFile(GlbPath, UnitScale, Warnings);
	if (!Model.IsValid())
	{
		AddError(FString::Printf(TEXT("GLB を読めない: %s"), *GlbPath));
		return false;
	}
	TSharedPtr<PmxPhysicsBuilder> B = PmxPhysicsBuilder::Build(Model);

	// ★再生時と同じ刻みにする (FAnimNode_MmdPhysics の既定)。
	// 切り分け用に環境変数で差し替えられる (既定は再生時の設定)。
	const auto EnvInt = [](const TCHAR* Name, int32 Default)
	{
		const FString V = FPlatformMisc::GetEnvironmentVariable(Name);
		return V.IsEmpty() ? Default : FCString::Atoi(*V);
	};
	const int32 FixedHz = EnvInt(TEXT("MMD_IDLE_FIXED_HZ"), 60);
	B->World.FixedTimeStep = 1.0f / static_cast<float>(FixedHz);
	B->World.SubSteps = EnvInt(TEXT("MMD_IDLE_SUBSTEPS"), 2);
	B->World.SolverIterations = EnvInt(TEXT("MMD_IDLE_ITER"), B->World.SolverIterations);
	B->World.SolveJointsFirst = EnvInt(TEXT("MMD_IDLE_JOINTS_FIRST"), 0) != 0;
	B->World.UseSplitImpulse = EnvInt(TEXT("MMD_IDLE_SPLIT"), B->World.UseSplitImpulse ? 1 : 0) != 0;
	B->World.UseJointSplitImpulse = EnvInt(TEXT("MMD_IDLE_JOINT_SPLIT"), 0) != 0;

	// 重力を切ると「重力 + 接触 + ジョイント制限のせめぎ合い」か
	// 「ソルバが素で注いでいる」かを切り分けられる。
	const FString GravEnv = FPlatformMisc::GetEnvironmentVariable(TEXT("MMD_IDLE_GRAVITY"));
	if (!GravEnv.IsEmpty()) B->World.Gravity = Vec3(0.0f, -FCString::Atof(*GravEnv), 0.0f);

	const FString Config = FString::Printf(
		TEXT("fixed=1/%d sub=%d iter=%d jointsFirst=%d split=%d jointSplit=%d"),
		FixedHz, B->World.SubSteps, B->World.SolverIterations,
		B->World.SolveJointsFirst ? 1 : 0, B->World.UseSplitImpulse ? 1 : 0,
		B->World.UseJointSplitImpulse ? 1 : 0);

	// 呼び出しは常に 60fps 相当で行う (刻みを変えても実時間の長さを揃えるため)。
	const int32 TotalFrames = FMath::RoundToInt(Seconds * 60.0f);
	const int32 WindowFrames = 60 * 5;   // 5 秒窓

	// 窓ごとの振れ幅を剛体別に取る。ボーンは一切動かさない (= 完全な静止入力)。
	TArray<FIdleTrack> Window;
	Window.SetNum(B->Bodies.Num());
	TArray<float> FirstWindow, LastWindow;
	FirstWindow.Init(0.0f, B->Bodies.Num());
	LastWindow.Init(0.0f, B->Bodies.Num());

	for (int32 f = 0; f < TotalFrames; f++)
	{
		B->World.StepSimulation(1.0f / 60.0f);
		for (int32 i = 0; i < B->Bodies.Num(); i++)
		{
			Window[i].Add(B->Bodies[i]->WorldTransform.Origin);
		}

		const int32 WindowIndex = f / WindowFrames;
		if ((f + 1) % WindowFrames == 0)
		{
			for (int32 i = 0; i < B->Bodies.Num(); i++)
			{
				if (WindowIndex == 0) FirstWindow[i] = Window[i].PeakToPeak();
				LastWindow[i] = Window[i].PeakToPeak();
				Window[i].Reset();
			}
		}
	}

	// PMX 単位 → cm (unitScale は 1 単位あたりのメートル)。
	const float ToCm = UnitScale * 100.0f;

	// 最後の窓で揺れている順に並べる。
	TArray<int32> Order;
	for (int32 i = 0; i < B->Bodies.Num(); i++) if (!B->Bodies[i]->IsKinematic()) Order.Add(i);
	Order.Sort([&LastWindow](int32 A, int32 C) { return LastWindow[A] > LastWindow[C]; });

	AddInfo(FString::Printf(TEXT("静止入力で %g 秒 [%s]。動的剛体 %d 件。"), Seconds, *Config, Order.Num()));
	AddInfo(TEXT("最後の 5 秒で振れが大きい剛体 (振れ幅 = 各軸の最大-最小):"));
	for (int32 k = 0; k < FMath::Min(10, Order.Num()); k++)
	{
		const int32 i = Order[k];
		AddInfo(FString::Printf(TEXT("  %-24s 最初の5秒 %.4f cm → 最後の5秒 %.4f cm"),
			*B->Bodies[i]->Name, FirstWindow[i] * ToCm, LastWindow[i] * ToCm));
	}

	float MaxLast = 0.0f, MaxFirst = 0.0f, SumLast = 0.0f;
	for (int32 i : Order)
	{
		MaxLast = FMath::Max(MaxLast, LastWindow[i]);
		MaxFirst = FMath::Max(MaxFirst, FirstWindow[i]);
		SumLast += LastWindow[i];
	}
	const float MeanLast = Order.Num() > 0 ? SumLast / Order.Num() : 0.0f;

	// ★総当たり用の 1 行サマリ (grep しやすいように固定書式)。
	AddInfo(FString::Printf(TEXT("IDLE_RESULT %s max_last=%.3f mean_last=%.3f max_first=%.3f"),
		*Config, MaxLast * ToCm, MeanLast * ToCm, MaxFirst * ToCm));

	// 静止入力なのだから、揺れは時間とともに減るのが正しい。
	// 増えているなら自励振動 (ソルバがエネルギーを注いでいる)。
	TestTrue(FString::Printf(TEXT("静止入力で揺れが増えていない (%.4f → %.4f cm)"),
		MaxFirst * ToCm, MaxLast * ToCm), MaxLast <= MaxFirst + 1e-4f);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
