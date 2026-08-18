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

	/** 波形の素性。振れ幅だけでは「ゆっくり大きく揺れる」と「細かく震える」を区別できない。 */
	struct FIdleWave
	{
		float Hz = 0.0f;              // 支配軸の平均まわりの往復回数 / 秒
		float MaxSpeedPerSec = 0.0f;  // 1 フレームの最大移動量を秒速へ直したもの
		int32 Axis = 0;               // 0=x 1=y 2=z
	};

	float AxisOf(const Vec3& V, int32 Axis) { return Axis == 0 ? V.x : (Axis == 1 ? V.y : V.z); }

	FIdleWave AnalyzeWave(const TArray<Vec3>& Samples, float DurationSeconds)
	{
		FIdleWave Out;
		if (Samples.Num() < 4 || DurationSeconds <= 0.0f) return Out;

		// 振れ幅が最大の軸を支配軸とする。
		float BestRange = -1.0f;
		for (int32 A = 0; A < 3; A++)
		{
			float Lo = FLT_MAX, Hi = -FLT_MAX;
			for (const Vec3& S : Samples)
			{
				const float V = AxisOf(S, A);
				Lo = FMath::Min(Lo, V); Hi = FMath::Max(Hi, V);
			}
			if (Hi - Lo > BestRange) { BestRange = Hi - Lo; Out.Axis = A; }
		}

		// 平均を基準に符号反転を数える。ノイズで数え過ぎないよう振れ幅の 10% を不感帯にする。
		float Mean = 0.0f;
		for (const Vec3& S : Samples) Mean += AxisOf(S, Out.Axis);
		Mean /= Samples.Num();

		const float Dead = BestRange * 0.1f;
		int32 Crossings = 0, Sign = 0;
		for (const Vec3& S : Samples)
		{
			const float D = AxisOf(S, Out.Axis) - Mean;
			if (D > Dead && Sign <= 0) { if (Sign != 0) Crossings++; Sign = 1; }
			else if (D < -Dead && Sign >= 0) { if (Sign != 0) Crossings++; Sign = -1; }
		}
		// 1 往復 = 反転 2 回。
		Out.Hz = (Crossings * 0.5f) / DurationSeconds;

		// フレーム間の最大移動量 (60fps で刻んでいるので 60 倍が秒速)。
		float MaxStep = 0.0f;
		for (int32 i = 1; i < Samples.Num(); i++)
		{
			MaxStep = FMath::Max(MaxStep, (Samples[i] - Samples[i - 1]).Length());
		}
		Out.MaxSpeedPerSec = MaxStep * 60.0f;
		return Out;
	}
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
	// ★ジョイントだけ追加で回す反復 (長い鎖の伝播用)。待機区間への影響をここで見る。
	B->World.JointVelocityIterations = EnvInt(TEXT("MMD_IDLE_JOINT_ITER"), B->World.JointVelocityIterations);
	// 位置補正速度の上限 (0 = コア既定の 10 を触らない)。再生時の設定を再現するのに使う。
	{
		const FString V = FPlatformMisc::GetEnvironmentVariable(TEXT("MMD_IDLE_MAXCORR"));
		if (!V.IsEmpty()) B->World.JointMaxCorrectionVel = FCString::Atof(*V);
	}
	B->World.SolveJointsFirst = EnvInt(TEXT("MMD_IDLE_JOINTS_FIRST"), 0) != 0;
	B->World.UseSplitImpulse = EnvInt(TEXT("MMD_IDLE_SPLIT"), B->World.UseSplitImpulse ? 1 : 0) != 0;
	B->World.UseJointSplitImpulse = EnvInt(TEXT("MMD_IDLE_JOINT_SPLIT"), 0) != 0;

	// 重力を切ると「重力 + 接触 + ジョイント制限のせめぎ合い」か
	// 「ソルバが素で注いでいる」かを切り分けられる。
	const FString GravEnv = FPlatformMisc::GetEnvironmentVariable(TEXT("MMD_IDLE_GRAVITY"));
	if (!GravEnv.IsEmpty()) B->World.Gravity = Vec3(0.0f, -FCString::Atof(*GravEnv), 0.0f);

	const FString Config = FString::Printf(
		TEXT("fixed=1/%d sub=%d iter=%d jointIter=%d jointsFirst=%d split=%d jointSplit=%d"),
		FixedHz, B->World.SubSteps, B->World.SolverIterations, B->World.JointVelocityIterations,
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

	// 最後の窓は波形をそのまま残す (周波数と速度を出すため)。
	const int32 SignalStart = TotalFrames - WindowFrames;
	TArray<TArray<Vec3>> Signal;
	Signal.SetNum(B->Bodies.Num());
	for (TArray<Vec3>& S : Signal) S.Reserve(WindowFrames);

	for (int32 f = 0; f < TotalFrames; f++)
	{
		B->World.StepSimulation(1.0f / 60.0f);
		for (int32 i = 0; i < B->Bodies.Num(); i++)
		{
			Window[i].Add(B->Bodies[i]->WorldTransform.Origin);
			if (f >= SignalStart) Signal[i].Add(B->Bodies[i]->WorldTransform.Origin);
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
	AddInfo(TEXT("最後の 5 秒 (振れ幅 / 周波数 / 最大速度):"));
	for (int32 k = 0; k < FMath::Min(10, Order.Num()); k++)
	{
		const int32 i = Order[k];
		const FIdleWave W = AnalyzeWave(Signal[i], WindowFrames / 60.0f);
		AddInfo(FString::Printf(
			TEXT("  %-22s 振れ %6.3f cm (最初 %6.3f) | %5.2f Hz | 最大 %6.2f cm/s | 軸 %s"),
			*B->Bodies[i]->Name, LastWindow[i] * ToCm, FirstWindow[i] * ToCm,
			W.Hz, W.MaxSpeedPerSec * ToCm, W.Axis == 0 ? TEXT("x") : (W.Axis == 1 ? TEXT("y") : TEXT("z"))));
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
