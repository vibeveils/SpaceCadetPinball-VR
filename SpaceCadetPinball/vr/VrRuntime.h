#pragma once
// VR presentation layer for Space Cadet Pinball.
//
// The original game still renders its 2D frame (table, score panel, ImGui menus) on the CPU.
// This module takes that frame, uploads it to Vulkan, and shows it inside a 3D scene through
// OpenXR: either on a virtual pinball cabinet (playfield on the angled top, score panel on the
// backbox) or on a large theater screen. It also reads the VR controllers through the OpenXR
// action system, including the Steam Frame controller interaction profile.

#include <cstdint>

namespace vr
{
	enum class ViewMode : int
	{
		Cabinet = 0,
		Theater = 1,
	};

	enum class Hand : int
	{
		Left = 0,
		Right = 1,
	};

	struct Settings
	{
		ViewMode Mode = ViewMode::Cabinet;
		float TiltDeg = 32.0f;        // playfield angle above horizontal
		float Distance = 0.30f;       // metres from the eyes to the front of the cabinet
		float HeightOffset = 0.0f;    // metres, added to the automatic table height
		float TableScale = 1.0f;      // 1.0 = 56 cm wide playfield
		float TheaterWidth = 2.6f;    // metres
		int RenderScale = 2;          // game frame resolution multiplier
	};

	// The game frame, as rendered by the SDL software renderer (SDL_PIXELFORMAT_ARGB8888 → BGRA bytes).
	struct FrameImage
	{
		const void* Pixels = nullptr;
		int Width = 0;
		int Height = 0;
		int Pitch = 0;
	};

	// Normalised rectangles (u0, v0, u1, v1) inside the frame image.
	struct FrameLayout
	{
		float Board[4] = {0, 0, 1, 1};
		float Sidebar[4] = {0, 0, 1, 1};
		float Game[4] = {0, 0, 1, 1};
	};

	struct Input
	{
		// Held states (the game turns transitions into press/release events).
		bool FlipperLeft = false;
		bool FlipperRight = false;
		bool Plunger = false;
		bool NudgeLeft = false;
		bool NudgeRight = false;
		bool NudgeUp = false;

		// One-shot events for this frame.
		bool PausePressed = false;
		bool MenuPressed = false;
		bool NewGameTriggered = false;
		bool BackPressed = false;

		// Laser pointer on the UI panel (frame pixel coordinates).
		bool PointerValid = false;
		float PointerX = 0;
		float PointerY = 0;
		bool PointerDown = false;
		float Scroll = 0;
	};

	bool Init(const Settings& settings);
	void Shutdown();
	bool IsActive();
	const char* LastError();

	// Pumps OpenXR events. Returns false when the application should exit.
	bool PollEvents();
	bool IsSessionRunning();
	bool IsSessionFocused();

	// Blocks until the runtime wants the next frame. Returns false if no frame was started.
	bool BeginFrame();
	void SyncInput(Input& out, bool uiPanelVisible);
	void EndFrame(const FrameImage& image, const FrameLayout& layout, bool uiPanelVisible);

	void Haptic(Hand hand, float amplitude, float seconds);
	void HapticBoth(float amplitude, float seconds);

	// Physical feedback when the table is nudged: shifts the cabinet briefly.
	void ShakeTable(float dx, float dz);

	Settings GetSettings();
	void SetSettings(const Settings& settings);
	void RequestRecenter();
	const char* InteractionProfileName();
}
