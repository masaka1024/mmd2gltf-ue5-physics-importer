// Copyright (c) 2026 masaka1024. MIT License.
//
// 検証A の基準出力を作る。移植元 C# エンジンで GLB を読み、N フレーム回して
// 全剛体のワールド姿勢を CSV へ落とす。UE 側の MmdPhysics.Core.GlbParity が
// 同じ入力・同じ手順で回した結果とこれを突き合わせる。
//
// ★外部からの駆動は一切行わない。
//   ボーンアニメーションを与えると「アニメの取り込み経路」の差まで混ざり、
//   物理エンジンの移植が正しいかどうかを切り分けられなくなる。
//   PhysicsWorld.AddBody が kinematic 剛体の KinematicTarget をバインド姿勢で
//   初期化するので、駆動なしでも「体は静止・揺れ物は重力で落ちる」状態を再現できる。
//
// 使い方:
//   dotnet run --project Tools/CsReference -- <glb> <frames> <out.csv>

using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Text;
using BulletPhysics;
using BulletPhysics.Pmx;

internal static class Program
{
    private static int Main(string[] args)
    {
        if (args.Length < 3)
        {
            Console.Error.WriteLine("usage: MmdCsReference <glb> <frames> <out.csv>");
            return 2;
        }

        string glbPath = args[0];
        int frames = int.Parse(args[1], CultureInfo.InvariantCulture);
        string outPath = args[2];

        var model = GlbPhysicsReader.LoadFile(glbPath, out float unitScale, out List<string> warnings);
        foreach (var w in warnings) Console.Error.WriteLine("[warn] " + w);
        Console.Error.WriteLine($"unitScale={unitScale.ToString("R", CultureInfo.InvariantCulture)} " +
                                $"bones={model.BoneNames.Count} bodies={model.RigidBodies.Count} joints={model.Joints.Count}");

        var builder = PmxPhysicsBuilder.Build(model);
        Console.Error.WriteLine($"built bodies={builder.Bodies.Count} joints={builder.World.Joints.Count} " +
                                $"pairs={builder.World.DebugCollisionPairCount}");

        for (int f = 0; f < frames; f++)
            builder.World.StepSimulation(1f / 30f);

        var sb = new StringBuilder();
        sb.Append("index,name,px,py,pz,qx,qy,qz,qw\n");
        for (int i = 0; i < builder.Bodies.Count; i++)
        {
            var b = builder.Bodies[i];
            var t = b.WorldTransform;
            sb.Append(i.ToString(CultureInfo.InvariantCulture)).Append(',');
            sb.Append(b.Name.Replace(',', '_')).Append(',');
            sb.Append(F(t.Origin.x)).Append(',').Append(F(t.Origin.y)).Append(',').Append(F(t.Origin.z)).Append(',');
            sb.Append(F(t.Rotation.x)).Append(',').Append(F(t.Rotation.y)).Append(',')
              .Append(F(t.Rotation.z)).Append(',').Append(F(t.Rotation.w)).Append('\n');
        }

        Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(outPath)));
        File.WriteAllText(outPath, sb.ToString(), new UTF8Encoding(false));
        Console.Error.WriteLine($"wrote {builder.Bodies.Count} rows -> {outPath}");
        return 0;
    }

    // "R" は float の往復可能な最短表現。UE 側は %.9g で出すので、比較は数値で行う。
    private static string F(float v) => v.ToString("R", CultureInfo.InvariantCulture);
}
