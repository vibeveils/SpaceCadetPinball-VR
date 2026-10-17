#pragma once
// Minimal vector / matrix helpers for the VR presentation layer.
// Column-major matrices (GLSL convention), right-handed, -Z forward (OpenXR convention).

#include <cmath>

namespace vrm
{
	struct Vec2 { float x, y; };

	struct Vec3
	{
		float x, y, z;
		Vec3() : x(0), y(0), z(0) {}
		Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
		Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
		Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
		Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
		Vec3 operator-() const { return {-x, -y, -z}; }
		Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
	};

	inline float Dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
	inline Vec3 Cross(const Vec3& a, const Vec3& b)
	{
		return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
	}
	inline float Length(const Vec3& a) { return std::sqrt(Dot(a, a)); }
	inline Vec3 Normalize(const Vec3& a)
	{
		float l = Length(a);
		return l > 1e-8f ? a * (1.0f / l) : Vec3{0, 0, 0};
	}
	inline Vec3 Lerp(const Vec3& a, const Vec3& b, float t) { return a + (b - a) * t; }

	struct Vec4 { float x, y, z, w; };

	struct Quat
	{
		float x, y, z, w;
		Quat() : x(0), y(0), z(0), w(1) {}
		Quat(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}

		static Quat AxisAngle(const Vec3& axis, float angle)
		{
			auto n = Normalize(axis);
			float s = std::sin(angle * 0.5f);
			return {n.x * s, n.y * s, n.z * s, std::cos(angle * 0.5f)};
		}

		Quat operator*(const Quat& q) const
		{
			return {
				w * q.x + x * q.w + y * q.z - z * q.y,
				w * q.y - x * q.z + y * q.w + z * q.x,
				w * q.z + x * q.y - y * q.x + z * q.w,
				w * q.w - x * q.x - y * q.y - z * q.z
			};
		}

		Vec3 Rotate(const Vec3& v) const
		{
			Vec3 u{x, y, z};
			Vec3 t = Cross(u, v) * 2.0f;
			return v + t * w + Cross(u, t);
		}
	};

	struct Pose
	{
		Quat orientation;
		Vec3 position;
		Vec3 Transform(const Vec3& p) const { return orientation.Rotate(p) + position; }
		Vec3 TransformDir(const Vec3& d) const { return orientation.Rotate(d); }
	};

	struct Mat4
	{
		float m[16]; // column-major

		static Mat4 Identity()
		{
			Mat4 r{};
			r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
			return r;
		}

		Mat4 operator*(const Mat4& b) const
		{
			Mat4 r{};
			for (int c = 0; c < 4; ++c)
				for (int rr = 0; rr < 4; ++rr)
				{
					float s = 0;
					for (int k = 0; k < 4; ++k)
						s += m[k * 4 + rr] * b.m[c * 4 + k];
					r.m[c * 4 + rr] = s;
				}
			return r;
		}

		static Mat4 FromPose(const Pose& p)
		{
			const auto& q = p.orientation;
			float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
			float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
			float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
			Mat4 r = Identity();
			r.m[0] = 1 - 2 * (yy + zz);
			r.m[1] = 2 * (xy + wz);
			r.m[2] = 2 * (xz - wy);
			r.m[4] = 2 * (xy - wz);
			r.m[5] = 1 - 2 * (xx + zz);
			r.m[6] = 2 * (yz + wx);
			r.m[8] = 2 * (xz + wy);
			r.m[9] = 2 * (yz - wx);
			r.m[10] = 1 - 2 * (xx + yy);
			r.m[12] = p.position.x;
			r.m[13] = p.position.y;
			r.m[14] = p.position.z;
			return r;
		}

		// Inverse of a rigid transform.
		static Mat4 ViewFromPose(const Pose& p)
		{
			Quat inv{-p.orientation.x, -p.orientation.y, -p.orientation.z, p.orientation.w};
			Pose ip;
			ip.orientation = inv;
			ip.position = -(inv.Rotate(p.position));
			return FromPose(ip);
		}

		// Vulkan clip space (Y down, Z 0..1) from OpenXR asymmetric FOV angles.
		static Mat4 ProjectionFov(float angleLeft, float angleRight, float angleUp, float angleDown,
		                          float nearZ, float farZ)
		{
			float tanL = std::tan(angleLeft), tanR = std::tan(angleRight);
			float tanU = std::tan(angleUp), tanD = std::tan(angleDown);
			float w = tanR - tanL;
			float h = tanD - tanU; // Vulkan: Y down => swap up/down
			Mat4 r{};
			r.m[0] = 2.0f / w;
			r.m[5] = 2.0f / h;
			r.m[8] = (tanR + tanL) / w;
			r.m[9] = (tanU + tanD) / h;
			r.m[10] = -farZ / (farZ - nearZ);
			r.m[11] = -1.0f;
			r.m[14] = -(farZ * nearZ) / (farZ - nearZ);
			return r;
		}
	};

	constexpr float Pi = 3.14159265358979f;
	inline float Deg(float d) { return d * Pi / 180.0f; }
	inline float Clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
}
