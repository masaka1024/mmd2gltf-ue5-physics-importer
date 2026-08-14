// Copyright (c) 2026 masaka1024. MIT License.

using UnrealBuildTool;

public class MmdPhysicsCore : ModuleRules
{
	public MmdPhysicsCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		// ★このモジュールだけ IEEE-754 厳密モードにする。
		//
		//   UE の既定は高速浮動小数点モードで、a*b+c の FMA 収縮や式の再結合が起きる。
		//   移植元 C# (RyuJIT) は演算ごとに厳密なので、既定のままだと 1〜2 ULP ずれる。
		//   単発では無視できる差だが、スカート剛体どうしが接触し続ける系では指数的に増幅し、
		//   実測で 1フレーム 2.4e-07 → 20フレーム 2.0e-04 → 60フレーム 5.4e-01 まで開いた。
		//   1:1 移植の目的は数値の一致なので、速度よりこちらを優先する。
		//   (物理は 117 剛体規模で、この設定による実測コストは問題にならない)
		FPSemantics = FPSemanticsMode.Precise;

		// このモジュールは移植元 C# の BulletPhysics / BulletPhysics.Pmx 名前空間の 1:1 移植であり、
		// UnityEngine に依存しなかったのと同じ理由で UE の Engine / Chaos にも依存させない。
		// 依存を Core / CoreUObject に絞ることで、数値パリティテストがヘッドレスで完結する。
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
		});
	}
}
