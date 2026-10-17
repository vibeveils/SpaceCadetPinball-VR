#pragma once
// Procedural geometry for the VR scene (cabinet, room, controllers, panels).

#include "vr_math.h"
#include <vector>
#include <cstdint>

namespace vr
{
	enum class Material : int
	{
		Lit = 0,
		Screen = 1,
		Glass = 2,
		Floor = 3,
		Sky = 4,
		Unlit = 5,
		Beam = 6,
		Planet = 7,
	};

	struct Vertex
	{
		float Pos[3];
		float Normal[3];
		float Uv[2];
		float Color[4];
	};

	struct DrawItem
	{
		uint32_t First = 0;
		uint32_t Count = 0;
		Material Mat = Material::Lit;
		float Emissive = 0;
		float UvRect[4] = {0, 0, 1, 1};
		bool Blend = false;
		float Brightness = 1.0f;
		float EdgeFade = 1.0f; // darken the outer edge of screen quads (off for artwork with its own frame)
	};

	struct Color
	{
		float r, g, b, a; // a = gloss for lit materials, opacity for beams
	};

	class SceneBuilder
	{
	public:
		std::vector<Vertex> Vertices;
		std::vector<DrawItem> Items;

		void Clear()
		{
			Vertices.clear();
			Items.clear();
		}

		// Starts a draw item; geometry added afterwards belongs to it until the next Begin.
		DrawItem& Begin(Material mat, float emissive = 0, bool blend = false);
		void End();

		void Tri(const vrm::Vec3& a, const vrm::Vec3& b, const vrm::Vec3& c,
		         const vrm::Vec2& ua, const vrm::Vec2& ub, const vrm::Vec2& uc,
		         const vrm::Vec3& n, const Color& col);
		// Corners counter-clockwise seen from the front: bottom-left, bottom-right, top-right, top-left.
		// Texture coordinates follow image convention (v grows downwards).
		void Quad(const vrm::Vec3& bl, const vrm::Vec3& br, const vrm::Vec3& tr, const vrm::Vec3& tl,
		          const Color& col);
		// Eight corners: 0..3 front face (bl, br, tr, tl), 4..7 the same corners on the back face.
		void Hexa(const vrm::Vec3 (&c)[8], const Color& col);
		void Box(const vrm::Pose& pose, const vrm::Vec3& center, const vrm::Vec3& half, const Color& col);
		void Sphere(const vrm::Vec3& center, float radius, int slices, int stacks, const Color& col, bool inward);
		void Disc(const vrm::Vec3& center, const vrm::Vec3& right, const vrm::Vec3& up, float rIn, float rOut,
		          int segments, const Color& col);
		// Ribbon pair along a segment, for soft beams (uv.x across, uv.y along).
		void Beam(const vrm::Vec3& from, const vrm::Vec3& to, float width, const Color& col);

	private:
		DrawItem* Current = nullptr;
	};
}
