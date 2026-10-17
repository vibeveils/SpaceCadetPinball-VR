#include "VrScene.h"

#include <utility>

using namespace vrm;

namespace vr
{
	DrawItem& SceneBuilder::Begin(Material mat, float emissive, bool blend)
	{
		End();
		Items.emplace_back();
		Current = &Items.back();
		Current->First = static_cast<uint32_t>(Vertices.size());
		Current->Mat = mat;
		Current->Emissive = emissive;
		Current->Blend = blend;
		return *Current;
	}

	void SceneBuilder::End()
	{
		if (Current)
		{
			Current->Count = static_cast<uint32_t>(Vertices.size()) - Current->First;
			if (Current->Count == 0)
				Items.pop_back();
			Current = nullptr;
		}
	}

	void SceneBuilder::Tri(const Vec3& a, const Vec3& b, const Vec3& c,
	                       const Vec2& ua, const Vec2& ub, const Vec2& uc,
	                       const Vec3& n, const Color& col)
	{
		auto push = [&](const Vec3& p, const Vec2& uv)
		{
			Vertex v{};
			v.Pos[0] = p.x;
			v.Pos[1] = p.y;
			v.Pos[2] = p.z;
			v.Normal[0] = n.x;
			v.Normal[1] = n.y;
			v.Normal[2] = n.z;
			v.Uv[0] = uv.x;
			v.Uv[1] = uv.y;
			v.Color[0] = col.r;
			v.Color[1] = col.g;
			v.Color[2] = col.b;
			v.Color[3] = col.a;
			Vertices.push_back(v);
		};
		push(a, ua);
		push(b, ub);
		push(c, uc);
	}

	void SceneBuilder::Quad(const Vec3& bl, const Vec3& br, const Vec3& tr, const Vec3& tl, const Color& col)
	{
		Vec3 n = Normalize(Cross(br - bl, tl - bl));
		if (Length(n) < 0.5f)
			n = Normalize(Cross(tr - br, tl - tr));
		Tri(bl, br, tr, {0, 1}, {1, 1}, {1, 0}, n, col);
		Tri(bl, tr, tl, {0, 1}, {1, 0}, {0, 0}, n, col);
	}

	void SceneBuilder::Hexa(const Vec3 (&c)[8], const Color& col)
	{
		Quad(c[0], c[1], c[2], c[3], col); // front
		Quad(c[5], c[4], c[7], c[6], col); // back
		Quad(c[4], c[0], c[3], c[7], col); // left
		Quad(c[1], c[5], c[6], c[2], col); // right
		Quad(c[3], c[2], c[6], c[7], col); // top
		Quad(c[4], c[5], c[1], c[0], col); // bottom
	}

	void SceneBuilder::Box(const Pose& pose, const Vec3& center, const Vec3& h, const Color& col)
	{
		Vec3 c[8] = {
			{-h.x, -h.y, h.z}, {h.x, -h.y, h.z}, {h.x, h.y, h.z}, {-h.x, h.y, h.z},
			{-h.x, -h.y, -h.z}, {h.x, -h.y, -h.z}, {h.x, h.y, -h.z}, {-h.x, h.y, -h.z},
		};
		for (auto& p : c)
			p = pose.Transform(p + center);
		Hexa(c, col);
	}

	void SceneBuilder::Sphere(const Vec3& center, float radius, int slices, int stacks, const Color& col,
	                          bool inward)
	{
		auto point = [&](int i, int j)
		{
			float theta = Pi * static_cast<float>(j) / stacks; // 0 top .. pi bottom
			float phi = 2.0f * Pi * static_cast<float>(i) / slices;
			return Vec3{std::sin(theta) * std::cos(phi), std::cos(theta), std::sin(theta) * std::sin(phi)};
		};
		for (int j = 0; j < stacks; ++j)
			for (int i = 0; i < slices; ++i)
			{
				Vec3 n00 = point(i, j), n10 = point(i + 1, j), n01 = point(i, j + 1), n11 = point(i + 1, j + 1);
				Vec3 p00 = center + n00 * radius, p10 = center + n10 * radius;
				Vec3 p01 = center + n01 * radius, p11 = center + n11 * radius;
				auto emit = [&](const Vec3& a, const Vec3& na, const Vec3& b, const Vec3& nb,
				                const Vec3& cc, const Vec3& nc)
				{
					// Per-vertex normals: push manually.
					for (auto pr : {std::make_pair(a, na), std::make_pair(b, nb), std::make_pair(cc, nc)})
					{
						Vertex v{};
						Vec3 nn = inward ? -pr.second : pr.second;
						v.Pos[0] = pr.first.x;
						v.Pos[1] = pr.first.y;
						v.Pos[2] = pr.first.z;
						v.Normal[0] = nn.x;
						v.Normal[1] = nn.y;
						v.Normal[2] = nn.z;
						v.Color[0] = col.r;
						v.Color[1] = col.g;
						v.Color[2] = col.b;
						v.Color[3] = col.a;
						Vertices.push_back(v);
					}
				};
				emit(p00, n00, p01, n01, p11, n11);
				emit(p00, n00, p11, n11, p10, n10);
			}
	}

	void SceneBuilder::Disc(const Vec3& center, const Vec3& right, const Vec3& up, float rIn, float rOut,
	                        int segments, const Color& col)
	{
		Vec3 n = Normalize(Cross(right, up));
		for (int i = 0; i < segments; ++i)
		{
			float a0 = 2.0f * Pi * i / segments, a1 = 2.0f * Pi * (i + 1) / segments;
			Vec3 d0 = right * std::cos(a0) + up * std::sin(a0);
			Vec3 d1 = right * std::cos(a1) + up * std::sin(a1);
			Vec3 i0 = center + d0 * rIn, i1 = center + d1 * rIn;
			Vec3 o0 = center + d0 * rOut, o1 = center + d1 * rOut;
			Tri(i0, o0, o1, {0, 0}, {1, 0}, {1, 1}, n, col);
			Tri(i0, o1, i1, {0, 0}, {1, 1}, {0, 1}, n, col);
		}
	}

	void SceneBuilder::Beam(const Vec3& from, const Vec3& to, float width, const Color& col)
	{
		Vec3 dir = Normalize(to - from);
		Vec3 ref = std::fabs(dir.y) < 0.9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
		Vec3 s1 = Normalize(Cross(dir, ref)) * (width * 0.5f);
		Vec3 s2 = Normalize(Cross(dir, s1)) * (width * 0.5f);
		for (const auto& s : {s1, s2})
		{
			Vec3 a = from - s, b = from + s, c = to + s, d = to - s;
			Vec3 n = Normalize(Cross(s, dir));
			Tri(a, b, c, {0, 0}, {1, 0}, {1, 1}, n, col);
			Tri(a, c, d, {0, 0}, {1, 1}, {0, 1}, n, col);
		}
	}
}
