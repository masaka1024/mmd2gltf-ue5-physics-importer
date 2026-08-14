// Copyright (c) 2026 masaka1024. MIT License.
//
// 移植元 MathTypes.cs は UnityEngine.Vector3 / UnityEngine.Matrix4x4 との
// explicit operator を持つ。コンソールから走らせるために最小限の型だけを補う。
// ★物理計算はこれらの型を一切使わない (境界の変換用に定義されているだけ) ので、
//   ここを差し替えても数値には影響しない。

namespace UnityEngine
{
    public struct Vector3
    {
        public float x, y, z;
        public Vector3(float x, float y, float z) { this.x = x; this.y = y; this.z = z; }
    }

    public struct Matrix4x4
    {
        public float m00, m01, m02, m03;
        public float m10, m11, m12, m13;
        public float m20, m21, m22, m23;
        public float m30, m31, m32, m33;
    }
}
