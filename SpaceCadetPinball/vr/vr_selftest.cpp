// Stand-alone check of the VR layer without the game data: renders a synthetic table frame.
// Usage: SpaceCadetPinballVRTest [frames] [cabinet|theater] [ui]
#include "VrRuntime.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <thread>
#include <chrono>

int main(int argc, char** argv)
{
	int frames = argc > 1 ? atoi(argv[1]) : 300;
	vr::Settings cfg;
	if (argc > 2 && !strcmp(argv[2], "theater"))
		cfg.Mode = vr::ViewMode::Theater;
	bool ui = argc > 3 && !strcmp(argv[3], "ui");

	if (!vr::Init(cfg))
	{
		printf("init failed: %s\n", vr::LastError());
		return 1;
	}

	// 600x416 table at 2x, like the game: 375px board + 225px sidebar.
	const int W = 1200, H = 832, boardW = 750;
	std::vector<uint32_t> px(W * H);
	for (int y = 0; y < H; ++y)
		for (int x = 0; x < W; ++x)
		{
			uint32_t r, g, b;
			if (x < boardW)
			{
				float fx = x / float(boardW), fy = y / float(H);
				r = uint32_t(20 + 60 * fy);
				g = uint32_t(30 + 90 * (1 - fy));
				b = uint32_t(90 + 120 * fx);
				if ((x / 30 + y / 30) % 2 == 0)
					r += 30;
				float dx = x - boardW * 0.5f, dy = y - H * 0.35f;
				if (dx * dx + dy * dy < 60 * 60)
					r = 240, g = 200, b = 40; // "bumper"
				if (y > H - 120 && std::fabs(dx) < 150 && std::fabs(dx) > 30)
					r = g = b = 230; // "flippers"
			}
			else
			{
				r = 10;
				g = 10;
				b = 30;
				if (y > 80 && y < 160 && x > boardW + 30 && x < W - 30)
					r = 220, g = 40, b = 40; // "score"
			}
			if (y < 22 && ui)
				r = g = b = 60; // "menu bar"
			px[y * W + x] = 0xFF000000u | (r << 16) | (g << 8) | b;
		}

	vr::FrameImage img{px.data(), W, H, W * 4};
	vr::FrameLayout layout;
	float sep = boardW / float(W);
	float b[4] = {0, 0, sep, 1}, s[4] = {sep, 0, 1, 1}, gm[4] = {0, 0, 1, 1};
	memcpy(layout.Board, b, sizeof b);
	memcpy(layout.Sidebar, s, sizeof s);
	memcpy(layout.Game, gm, sizeof gm);

	int rendered = 0;
	for (int i = 0; i < frames * 20 && rendered < frames; ++i)
	{
		if (!vr::PollEvents())
			break;
		if (!vr::IsSessionRunning())
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
			continue;
		}
		if (!vr::BeginFrame())
			continue;
		vr::Input in;
		vr::SyncInput(in, ui);
		vr::EndFrame(img, layout, ui);
		rendered++;
	}
	printf("rendered %d frames, profile %s\n", rendered, vr::InteractionProfileName());
	vr::Shutdown();
	return rendered > 0 ? 0 : 2;
}
