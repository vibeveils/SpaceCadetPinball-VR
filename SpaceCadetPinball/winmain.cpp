#include "pch.h"
#include "winmain.h"

#include "control.h"
#include "EmbeddedData.h"
#include "fullscrn.h"
#include "midi.h"
#include "options.h"
#include "pb.h"
#include "render.h"
#include "Sound.h"
#include "translations.h"
#include "font_selection.h"
#include "TPinballTable.h"
#include "proj.h"
#include <cfloat>

#ifdef SCP_VR
#include "vr/VrRuntime.h"
#endif

constexpr const char* winmain::Version;

SDL_Window* winmain::MainWindow = nullptr;
SDL_Renderer* winmain::Renderer = nullptr;
ImGuiIO* winmain::ImIO = nullptr;

int winmain::return_value = 0;
bool winmain::bQuit = false;
bool winmain::activated = false;
bool winmain::DispFrameRate = false;
bool winmain::DispGRhistory = false;
bool winmain::single_step = false;
bool winmain::has_focus = true;
int winmain::last_mouse_x;
int winmain::last_mouse_y;
int winmain::mouse_down;
bool winmain::no_time_loss = false;

bool winmain::restart = false;

std::vector<float> winmain::gfrDisplay{};
unsigned winmain::gfrOffset = 0;
float winmain::gfrWindow = 5.0f;
bool winmain::ShowAboutDialog = false;
bool winmain::ShowImGuiDemo = false;
bool winmain::ShowSpriteViewer = false;
bool winmain::ShowExitPopup = false;
bool winmain::LaunchBallEnabled = true;
bool winmain::HighScoresEnabled = true;
bool winmain::DemoActive = false;
int winmain::MainMenuHeight = 0;
std::string winmain::FpsDetails, winmain::PrevSdlError;
unsigned winmain::PrevSdlErrorCount = 0;
double winmain::UpdateToFrameRatio;
winmain::DurationMs winmain::TargetFrameTime;
optionsStruct& winmain::Options = options::Options;
winmain::DurationMs winmain::SpinThreshold = DurationMs(0.005);
WelfordState winmain::SleepState{};
int winmain::CursorIdleCounter = 0;
bool winmain::VrMode = false;
float winmain::FontSizeMultiplier = 1.0f;
SDL_Surface* winmain::VrSurface = nullptr;
int winmain::VrRenderScale = 2;
bool winmain::VrMenuPaused = false;
int winmain::ControllerTriggerState[2] = {0, 0};

int winmain::WinMain(LPCSTR lpCmdLine)
{
	std::set_new_handler(memalloc_failure);

	printf("Game version: %s\n", Version);
	printf("Command line: %s\n", lpCmdLine);
	printf("Compiled with: SDL %d.%d.%d;", SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_PATCHLEVEL);
	printf(" SDL_mixer %d.%d.%d;", SDL_MIXER_MAJOR_VERSION, SDL_MIXER_MINOR_VERSION, SDL_MIXER_PATCHLEVEL);
	printf(" ImGui %s %s\n", IMGUI_VERSION, ImGuiRender);

	// VR mode: "-vr" on the command line or SCP_VR=1 in the environment.
	{
		auto envVr = getenv("SCP_VR");
		VrMode = strstr(lpCmdLine, "-vr") != nullptr || (envVr && envVr[0] == '1');
		auto scaleArg = strstr(lpCmdLine, "-vrscale=");
		if (scaleArg)
			VrRenderScale = Clamp(atoi(scaleArg + 9), 1, 4);
#ifndef SCP_VR
		if (VrMode)
		{
			printf("This build has no VR support, starting in desktop mode.\n");
			VrMode = false;
		}
#endif
	}

	// In VR the game draws into an off-screen surface, so no desktop window is needed.
	if (VrMode && !getenv("SDL_VIDEODRIVER"))
		SDL_SetHint(SDL_HINT_VIDEODRIVER, "offscreen");

	// SDL init
	bool forceNoAudio = false;
	SDL_SetMainReady();
	const Uint32 sdlFlags = SDL_INIT_TIMER | SDL_INIT_AUDIO | SDL_INIT_VIDEO |
		SDL_INIT_EVENTS | SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER;
	auto sdlInit = SDL_Init(sdlFlags);
	if (sdlInit < 0 && VrMode)
	{
		// Offscreen driver not compiled into this SDL: fall back to a hidden normal window.
		SDL_SetHint(SDL_HINT_VIDEODRIVER, "");
		sdlInit = SDL_Init(sdlFlags);
	}
	if (sdlInit < 0)
	{
		// No usable audio device should not stop the game from starting.
		printf("SDL_Init failed (%s), retrying without audio.\n", SDL_GetError());
		sdlInit = SDL_Init(sdlFlags & ~SDL_INIT_AUDIO);
		if (sdlInit >= 0)
			forceNoAudio = true;
	}
	if (sdlInit < 0)
	{
		pb::ShowMessageBox(SDL_MESSAGEBOX_ERROR, "Could not initialize SDL2", SDL_GetError());
		return 1;
	}

	pb::quickFlag = strstr(lpCmdLine, "-quick") != nullptr;

	// SDL window
	SDL_Window* window = SDL_CreateWindow
	(
		pb::get_rc_string(Msg::STRING139),
		SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
		800, 556,
		SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE
	);
	MainWindow = window;
	if (!window)
	{
		pb::ShowMessageBox(SDL_MESSAGEBOX_ERROR, "Could not create window", SDL_GetError());
		return 1;
	}

	// If HW fails, fallback to SW SDL renderer.
	SDL_Renderer* renderer = nullptr;
	if (VrMode)
	{
		// The frame is composed on the CPU and handed to the VR renderer as a texture.
		// Extra rows at the bottom leave room for the menu bar without rescaling the table.
		const int vrW = 600 * VrRenderScale, vrH = (416 + 24) * VrRenderScale;
		VrSurface = SDL_CreateRGBSurfaceWithFormat(0, vrW, vrH, 32, SDL_PIXELFORMAT_ARGB8888);
		if (VrSurface)
			Renderer = renderer = SDL_CreateSoftwareRenderer(VrSurface);
		SDL_SetWindowSize(window, vrW, vrH);
	}
	else
	{
		auto swOffset = strstr(lpCmdLine, "-sw") != nullptr ? 1 : 0;
		for (int i = swOffset; i < 2 && !renderer; i++)
		{
			Renderer = renderer = SDL_CreateRenderer
			(
				window,
				-1,
				i == 0 ? SDL_RENDERER_ACCELERATED : SDL_RENDERER_SOFTWARE
			);
		}
	}
	if (!renderer)
	{
		pb::ShowMessageBox(SDL_MESSAGEBOX_ERROR, "Could not create renderer", SDL_GetError());
		return 1;
	}
	SDL_RendererInfo rendererInfo{};
	if (!SDL_GetRendererInfo(renderer, &rendererInfo))
		printf("Using SDL renderer: %s\n", rendererInfo.name);
	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");

	auto prefPath = SDL_GetPrefPath("", "SpaceCadetPinball");
	auto basePath = SDL_GetBasePath();

	// SDL mixer init
	bool mixOpened = false, noAudio = forceNoAudio || strstr(lpCmdLine, "-noaudio") != nullptr;
	if (!noAudio)
	{
		if ((Mix_Init(MIX_INIT_MID_Proxy) & MIX_INIT_MID_Proxy) == 0)
		{
			printf("Could not initialize SDL MIDI, music might not work.\nSDL Error: %s\n", SDL_GetError());
			SDL_ClearError();
		}
		if (Mix_OpenAudio(MIX_DEFAULT_FREQUENCY, MIX_DEFAULT_FORMAT, 2, 1024) != 0)
		{
			printf("Could not open audio device, continuing without audio.\nSDL Error: %s\n", SDL_GetError());
			SDL_ClearError();
		}
		else
			mixOpened = true;
	}

	{
		// Load SDL Game Controller definitions from DB
		unsigned decompressedSize{};
		const auto controllerDb = ImFontAtlas::DecompressCompressedStbData(
			EmbeddedData::SDL_GameControllerDB_compressed_data,
			EmbeddedData::SDL_GameControllerDB_compressed_size,
			decompressedSize);
		auto rw = SDL_RWFromMem(controllerDb, decompressedSize);
		const auto added = SDL_GameControllerAddMappingsFromRW(rw, 1);
		IM_FREE(controllerDb);
		if (added < 0)
		{
			printf("Could not load game controller DB.\nSDL Error: %s\n", SDL_GetError());
			SDL_ClearError();
		}
	}

	auto resetAllOptions = strstr(lpCmdLine, "-reset") != nullptr;
	do
	{
		restart = false;

		// ImGui init
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO();
		ImIO = &io;
		auto iniPath = std::string(prefPath) + "imgui_pb.ini";
		io.IniFilename = iniPath.c_str();

		// First option initialization step: just load settings from .ini. Needs ImGui context.
		options::InitPrimary();
		if (resetAllOptions)
		{
			resetAllOptions = false;
			options::ResetAllOptions();
		}

#ifdef SCP_VR
		// VR overrides some desktop settings; remember them so the desktop setup is not changed.
		const bool savedLinearFiltering = Options.LinearFiltering;
		const bool savedFullScreen = Options.FullScreen;
		const bool savedShowMenu = Options.ShowMenu;
		if (VrMode)
		{
			// Nearest-neighbour on the CPU keeps the pixel art crisp; the GPU does the filtering in 3D.
			Options.LinearFiltering.V = false;
			Options.FullScreen.V = false;
			Options.ShowMenu.V = false;
			FontSizeMultiplier = static_cast<float>(VrRenderScale);
		}
#endif

		if (!Options.FontFileName.V.empty())
		{
			ImVector<ImWchar> ranges;
			translations::GetGlyphRange(&ranges);
			ImFontConfig fontConfig{};

			// ToDo: further tweak font options, maybe try imgui_freetype
			fontConfig.OversampleV = 2;
			fontConfig.OversampleH = 4;

			// ToDo: improve font file test, checking if file exists is not enough
			auto fontLoaded = false;
			auto fileName = Options.FontFileName.V.c_str();
			auto fileHandle = fopenu(fileName, "rb");
			if (fileHandle)
			{
				fclose(fileHandle);

				// ToDo: Bind font size to UI scale
				if (io.Fonts->AddFontFromFileTTF(fileName, 13.f * FontSizeMultiplier, &fontConfig, ranges.Data))
					fontLoaded = true;
			}

			if (!fontLoaded)
				printf("Failed to load font: %s, using embedded font.\n", fileName);
			io.Fonts->Build();
		}
		if (FontSizeMultiplier != 1.0f && io.Fonts->Fonts.empty())
		{
			// Embedded pixel font at an integer multiple stays sharp on the high resolution VR frame.
			ImFontConfig fontConfig{};
			fontConfig.SizePixels = 13.0f * FontSizeMultiplier;
			io.Fonts->AddFontDefault(&fontConfig);
			io.Fonts->Build();
		}
		ImGui_Render_Init(renderer);
		ImGui::StyleColorsDark();
		if (FontSizeMultiplier != 1.0f)
			ImGui::GetStyle().ScaleAllSizes(FontSizeMultiplier);
		if (VrMode)
			io.MouseDrawCursor = true;

		ImGui_ImplSDL2_InitForSDLRenderer(window, Renderer);
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;

		// Data search order: WD, WD/assets, executable path, executable path/assets, user pref path,
		// platform specific paths. The "assets" folders keep the game files apart from the program files.
		const std::string baseAssetsPath = std::string(basePath ? basePath : "") + "assets/";
		std::vector<const char*> searchPaths
		{
			{
				"",
				"assets/",
				basePath,
				basePath ? baseAssetsPath.c_str() : nullptr,
				prefPath
			}
		};
		searchPaths.insert(searchPaths.end(), std::begin(PlatformDataPaths), std::end(PlatformDataPaths));
		pb::SelectDatFile(searchPaths);

		// Second step: run updates that depend on .DAT file selection
		options::InitSecondary();

		Sound::Init(mixOpened, Options.SoundChannels, Options.Sounds, Options.SoundVolume);
		if (!mixOpened)
			Options.Sounds = false;

		if (!midi::music_init(mixOpened, Options.MusicVolume))
			Options.Music = false;

		if (pb::init())
		{
			std::string message = "The .dat file is missing.\n"
				"Make sure that the game data is present in any of the following locations:\n";
			for (auto path : searchPaths)
			{
				if (path)
				{
					message = message + (path[0] ? path : "working directory") + "\n";
				}
			}
			pb::ShowMessageBox(SDL_MESSAGEBOX_ERROR, "Could not load game data", message.c_str());
			return 1;
		}

#ifdef SCP_VR
		// Start OpenXR once the game data is known to be present, so a missing .dat file is
		// reported on the desktop instead of in an empty headset.
		if (VrMode && !vr::IsActive())
		{
			VrSyncSettings(true);
			if (!vr::Init(vr::GetSettings()))
			{
				std::string msg = std::string("Could not start VR (OpenXR).\n\n") + vr::LastError() +
					"\n\nMake sure SteamVR (or another OpenXR runtime) is running, or start the game "
					"without -vr for desktop mode.";
				pb::ShowMessageBox(SDL_MESSAGEBOX_ERROR, "Space Cadet Pinball VR", msg.c_str());
				return 1;
			}
		}
#endif

		fullscrn::init();

		pb::reset_table();
		pb::firsttime_setup();

		if (strstr(lpCmdLine, "-fullscreen"))
		{
			Options.FullScreen = true;
		}

		if (!VrMode)
		{
			if (!Options.FullScreen)
			{
				auto resInfo = &fullscrn::resolution_array[fullscrn::GetResolution()];
				SDL_SetWindowSize(MainWindow, resInfo->TableWidth, resInfo->TableHeight);
			}
			SDL_ShowWindow(window);
			fullscrn::set_screen_mode(Options.FullScreen);
		}
		else
		{
			fullscrn::window_size_changed();
			Sound::Activate();
			if (Options.Music)
				midi::music_play();
		}

		if (strstr(lpCmdLine, "-demo"))
			pb::toggle_demo();
		else
			pb::replay_level(false);

		if (VrMode)
			VrMainLoop();
		else
			MainLoop();

#ifdef SCP_VR
		if (VrMode)
		{
			VrSyncSettings(false);
			Options.LinearFiltering.V = savedLinearFiltering;
			Options.FullScreen.V = savedFullScreen;
			Options.ShowMenu.V = savedShowMenu;
		}
#endif
		options::uninit();
		midi::music_shutdown();
		Sound::Close();
		pb::uninit();

		ImGui_Render_Shutdown();
		ImGui_ImplSDL2_Shutdown();
		ImGui::DestroyContext();
	}
	while (restart);

	if (!noAudio)
	{
		if (mixOpened)
			Mix_CloseAudio();
		Mix_Quit();
	}

	SDL_free(basePath);
	SDL_free(prefPath);
#ifdef SCP_VR
	vr::Shutdown();
#endif
	SDL_DestroyRenderer(renderer);
	if (VrSurface)
		SDL_FreeSurface(VrSurface);
	SDL_DestroyWindow(window);
	SDL_Quit();

	return return_value;
}

void winmain::MainLoop()
{
	bQuit = false;
	unsigned updateCounter = 0, frameCounter = 0;
	auto frameStart = Clock::now();
	double UpdateToFrameCounter = 0;
	DurationMs sleepRemainder(0), frameDuration(TargetFrameTime);
	auto prevTime = frameStart;

	while (true)
	{
		if (DispFrameRate)
		{
			auto curTime = Clock::now();
			if (curTime - prevTime > DurationMs(1000))
			{
				char buf[60];
				auto elapsedSec = DurationMs(curTime - prevTime).count() * 0.001;
				snprintf(buf, sizeof buf, "Updates/sec = %02.02f Frames/sec = %02.02f ",
				         updateCounter / elapsedSec, frameCounter / elapsedSec);
				SDL_SetWindowTitle(MainWindow, buf);
				FpsDetails = buf;
				frameCounter = updateCounter = 0;
				prevTime = curTime;
			}
		}

		if (!ProcessWindowMessages() || bQuit)
			break;

		if (has_focus)
		{
			if (mouse_down)
			{
				int x, y, w, h;
				SDL_GetMouseState(&x, &y);
				SDL_GetWindowSize(MainWindow, &w, &h);
				float dx = static_cast<float>(last_mouse_x - x) / static_cast<float>(w);
				float dy = static_cast<float>(y - last_mouse_y) / static_cast<float>(h);
				pb::ballset(dx, dy);

				// Original creates continuous mouse movement with mouse capture.
				// Alternative solution: mouse warp at window edges.
				int xMod = 0, yMod = 0;
				if (x == 0 || x >= w - 1)
					xMod = w - 2;
				if (y == 0 || y >= h - 1)
					yMod = h - 2;
				if (xMod != 0 || yMod != 0)
				{
					// Mouse warp does not work over remote desktop or in some VMs
					x = abs(x - xMod);
					y = abs(y - yMod);
					SDL_WarpMouseInWindow(MainWindow, x, y);
				}

				last_mouse_x = x;
				last_mouse_y = y;
			}
			if (!single_step && !no_time_loss)
			{
				auto dt = static_cast<float>(frameDuration.count());
				pb::frame(dt);
				if (DispGRhistory)
				{
					auto targetSize = static_cast<unsigned>(static_cast<float>(Options.UpdatesPerSecond) * gfrWindow);
					if (gfrDisplay.size() != targetSize)
					{
						gfrDisplay.resize(targetSize, static_cast<float>(TargetFrameTime.count()));
						gfrOffset = 0;
					}
					gfrDisplay[gfrOffset] = dt;
					gfrOffset = (gfrOffset + 1) % gfrDisplay.size();
				}
				updateCounter++;
			}
			no_time_loss = false;

			if (UpdateToFrameCounter >= UpdateToFrameRatio)
			{
				if (Options.HideCursor && CursorIdleCounter <= 0)
					ImGui::SetMouseCursor(ImGuiMouseCursor_None);
				ImGui_ImplSDL2_NewFrame();
				ImGui_Render_NewFrame();
				ImGui::NewFrame();
				RenderUi();

				SDL_RenderClear(Renderer);
				// Alternative clear hack, clear might fail on some systems
				// Todo: remove original clear, if save for all platforms
				SDL_RenderFillRect(Renderer, nullptr);
				render::PresentVScreen();

				ImGui::Render();
				ImGui_Render_RenderDrawData(ImGui::GetDrawData());

				SDL_RenderPresent(Renderer);
				frameCounter++;
				UpdateToFrameCounter -= UpdateToFrameRatio;
			}

			auto sdlError = SDL_GetError();
			if (sdlError[0] || !PrevSdlError.empty())
			{
				if (sdlError[0])
					SDL_ClearError();

				// Rate limit duplicate SDL error messages.
				if (sdlError != PrevSdlError)
				{
					PrevSdlError = sdlError;
					if (PrevSdlErrorCount > 0)
					{
						printf("SDL Error: ^ Previous Error Repeated %u Times\n", PrevSdlErrorCount + 1);
						PrevSdlErrorCount = 0;
					}

					if (sdlError[0])
						printf("SDL Error: %s\n", sdlError);
				}
				else
				{
					PrevSdlErrorCount++;
				}
			}

			auto updateEnd = Clock::now();
			auto targetTimeDelta = TargetFrameTime - DurationMs(updateEnd - frameStart) - sleepRemainder;

			TimePoint frameEnd;
			if (targetTimeDelta > DurationMs::zero() && !Options.UncappedUpdatesPerSecond)
			{
				if (Options.HybridSleep)
					HybridSleep(targetTimeDelta);
				else
					std::this_thread::sleep_for(targetTimeDelta);
				frameEnd = Clock::now();
			}
			else
			{
				frameEnd = updateEnd;
			}

			// Limit duration to 2 * target time
			sleepRemainder = Clamp(DurationMs(frameEnd - updateEnd) - targetTimeDelta, -TargetFrameTime,
			                       TargetFrameTime);
			frameDuration = std::min<DurationMs>(DurationMs(frameEnd - frameStart), 2 * TargetFrameTime);
			frameStart = frameEnd;
			UpdateToFrameCounter++;

			CursorIdleCounter = std::max(CursorIdleCounter - static_cast<int>(frameDuration.count()), 0);
		}
	}

	if (PrevSdlErrorCount > 0)
	{
		printf("SDL Error: ^ Previous Error Repeated %u Times\n", PrevSdlErrorCount);
	}
}

void winmain::RenderUi()
{
	// Transparent menu bar with a button for preventing menu lockout.
	ImGui::PushStyleColor(ImGuiCol_MenuBarBg, ImVec4{});
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4{});
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
	if (!Options.ShowMenu && !VrMode && ImGui::BeginMainMenuBar())
	{
		if (ImGui::MenuItem("Menu"))
		{
			options::toggle(Menu1::Show_Menu);
			ImGui::FocusWindow(nullptr);
		}
		ImGui::EndMainMenuBar();
	}
	ImGui::PopStyleVar(1);
	ImGui::PopStyleColor(2);

	// No demo window in release to save space
#ifndef NDEBUG
	if (ShowImGuiDemo)
		ImGui::ShowDemoWindow(&ShowImGuiDemo);
#endif

	if (Options.ShowMenu && ImGui::BeginMainMenuBar())
	{
		int currentMenuHeight = static_cast<int>(ImGui::GetWindowSize().y);
		if (MainMenuHeight != currentMenuHeight)
		{
			// Get the height of the main menu bar and update screen coordinates
			MainMenuHeight = currentMenuHeight;
			fullscrn::window_size_changed();
		}

		if (ImGui::BeginMenu(pb::get_rc_string(Msg::Menu1_Game)))
		{
			ImGuiMenuItemWShortcut(GameBindings::NewGame);
			if (ImGui::MenuItem(pb::get_rc_string(Msg::Menu1_Launch_Ball), nullptr, false, LaunchBallEnabled))
			{
				end_pause();
				pb::launch_ball();
			}
			ImGuiMenuItemWShortcut(GameBindings::TogglePause);
			ImGui::Separator();

			if (ImGui::MenuItem(pb::get_rc_string(Msg::Menu1_High_Scores), nullptr, false, HighScoresEnabled))
			{
				pause(false);
				pb::high_scores();
			}
			if (ImGui::MenuItem(pb::get_rc_string(Msg::Menu1_Demo), nullptr, DemoActive))
			{
				end_pause();
				pb::toggle_demo();
			}
			ImGuiMenuItemWShortcut(GameBindings::Exit);
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu(pb::get_rc_string(Msg::Menu1_Options)))
		{
			ImGuiMenuItemWShortcut(GameBindings::ToggleMenuDisplay, Options.ShowMenu);
			ImGuiMenuItemWShortcut(GameBindings::ToggleFullScreen, Options.FullScreen);
			if (ImGui::BeginMenu(pb::get_rc_string(Msg::Menu1_Select_Players)))
			{
				if (ImGui::MenuItem(pb::get_rc_string(Msg::Menu1_1Player), nullptr, Options.Players == 1))
				{
					options::toggle(Menu1::OnePlayer);
					new_game();
				}
				if (ImGui::MenuItem(pb::get_rc_string(Msg::Menu1_2Players), nullptr, Options.Players == 2))
				{
					options::toggle(Menu1::TwoPlayers);
					new_game();
				}
				if (ImGui::MenuItem(pb::get_rc_string(Msg::Menu1_3Players), nullptr, Options.Players == 3))
				{
					options::toggle(Menu1::ThreePlayers);
					new_game();
				}
				if (ImGui::MenuItem(pb::get_rc_string(Msg::Menu1_4Players), nullptr, Options.Players == 4))
				{
					options::toggle(Menu1::FourPlayers);
					new_game();
				}
				ImGui::EndMenu();
			}
			ImGuiMenuItemWShortcut(GameBindings::ShowControlDialog);
			if (ImGui::BeginMenu("Language"))
			{
				auto currentLanguage = translations::GetCurrentLanguage();
				for (auto& item : translations::Languages)
				{
					if (ImGui::MenuItem(item.DisplayName, nullptr, currentLanguage->Language == item.Language))
					{
						if (currentLanguage->Language != item.Language)
						{
							translations::SetCurrentLanguage(item.ShortName);
							Restart();
						}
					}
				}
				ImGui::EndMenu();
			}
			ImGui::Separator();

			if (ImGui::BeginMenu("Audio"))
			{
				ImGuiMenuItemWShortcut(GameBindings::ToggleSounds, Options.Sounds);
				if (ImGui::MenuItem("Stereo Sound Effects", nullptr, Options.SoundStereo))
				{
					options::toggle(Menu1::SoundStereo);
				}
				ImGui::TextUnformatted("Sound Volume");
				if (ImGui::SliderInt("##Sound Volume", &Options.SoundVolume.V, options::MinVolume, options::MaxVolume,
				                     "%d",
				                     ImGuiSliderFlags_AlwaysClamp))
				{
					Sound::SetVolume(Options.SoundVolume);
				}
				ImGui::TextUnformatted("Sound Channels");
				if (ImGui::SliderInt("##Sound Channels", &Options.SoundChannels.V, options::MinSoundChannels,
				                     options::MaxSoundChannels, "%d", ImGuiSliderFlags_AlwaysClamp))
				{
					Sound::SetChannels(Options.SoundChannels);
				}
				ImGui::Separator();

				ImGuiMenuItemWShortcut(GameBindings::ToggleMusic, Options.Music);
				ImGui::TextUnformatted("Music Volume");
				if (ImGui::SliderInt("##Music Volume", &Options.MusicVolume.V, options::MinVolume, options::MaxVolume,
				                     "%d",
				                     ImGuiSliderFlags_AlwaysClamp))
				{
					midi::SetVolume(Options.MusicVolume);
				}
				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu("Graphics"))
			{
				if (ImGui::MenuItem(pb::get_rc_string(Msg::Menu1_WindowUniformScale), nullptr, Options.UniformScaling))
				{
					options::toggle(Menu1::WindowUniformScale);
				}
				if (ImGui::MenuItem("Linear Filtering", nullptr, Options.LinearFiltering))
				{
					options::toggle(Menu1::WindowLinearFilter);
				}
				if (ImGui::MenuItem("Integer Scaling", nullptr, Options.IntegerScaling))
				{
					options::toggle(Menu1::WindowIntegerScale);
				}
				if (ImGui::DragFloat("UI Scale", &Options.UIScale.V, 0.005f, 0.8f, 5,
				                     "%.2f", ImGuiSliderFlags_AlwaysClamp))
				{
					ImIO->FontGlobalScale = Options.UIScale;
				}
				ImGui::Separator();

				char buffer[80]{};
				auto changed = false;
				if (ImGui::MenuItem("Set Default UPS/FPS"))
				{
					changed = true;
					Options.UpdatesPerSecond = options::DefUps;
					Options.FramesPerSecond = options::DefFps;
				}
				if (ImGui::SliderInt("UPS", &Options.UpdatesPerSecond.V, options::MinUps, options::MaxUps, "%d",
				                     ImGuiSliderFlags_AlwaysClamp))
				{
					changed = true;
					Options.FramesPerSecond = std::min(Options.UpdatesPerSecond.V, Options.FramesPerSecond.V);
				}
				if (ImGui::SliderInt("FPS", &Options.FramesPerSecond.V, options::MinFps, options::MaxFps, "%d",
				                     ImGuiSliderFlags_AlwaysClamp))
				{
					changed = true;
					Options.UpdatesPerSecond = std::max(Options.UpdatesPerSecond.V, Options.FramesPerSecond.V);
				}
				snprintf(buffer, sizeof buffer - 1, "Uncapped UPS (FPS ratio %02.02f)", UpdateToFrameRatio);
				if (ImGui::MenuItem(buffer, nullptr, Options.UncappedUpdatesPerSecond))
				{
					Options.UncappedUpdatesPerSecond ^= true;
				}
				if (ImGui::MenuItem("Precise Sleep", nullptr, Options.HybridSleep))
				{
					Options.HybridSleep ^= true;
					SleepState = WelfordState{};
					SpinThreshold = DurationMs::zero();
				}

				if (changed)
				{
					UpdateFrameRate();
				}
				ImGui::Separator();

				if (ImGui::MenuItem("Hide Cursor", nullptr, Options.HideCursor))
				{
					Options.HideCursor ^= true;
				}
				if (ImGui::MenuItem("Change Font..."))
				{
					font_selection::ShowDialog();
				}

				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu(pb::get_rc_string(Msg::Menu1_Table_Resolution)))
			{
				char buffer[20]{};
				auto resolutionStringId = Msg::Menu1_UseMaxResolution_640x480;

				switch (fullscrn::GetMaxResolution())
				{
				case 0: resolutionStringId = Msg::Menu1_UseMaxResolution_640x480;
					break;
				case 1: resolutionStringId = Msg::Menu1_UseMaxResolution_800x600;
					break;
				case 2: resolutionStringId = Msg::Menu1_UseMaxResolution_1024x768;
					break;
				}

				auto maxResText = pb::get_rc_string(resolutionStringId);
				if (ImGui::MenuItem(maxResText, nullptr, Options.Resolution == -1))
				{
					options::toggle(Menu1::MaximumResolution);
				}
				for (auto i = 0; i <= fullscrn::GetMaxResolution(); i++)
				{
					auto& res = fullscrn::resolution_array[i];
					snprintf(buffer, sizeof buffer - 1, "%d x %d", res.ScreenWidth, res.ScreenHeight);
					if (ImGui::MenuItem(buffer, nullptr, Options.Resolution == i))
					{
						options::toggle(static_cast<Menu1>(static_cast<int>(Menu1::R640x480) + i));
					}
				}
				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu("Game Data"))
			{
				if (ImGui::MenuItem("Prefer 3DPB Data", nullptr, Options.Prefer3DPBGameData))
				{
					options::toggle(Menu1::Prefer3DPBGameData);
				}
				ImGui::EndMenu();
			}
			ImGui::Separator();
			if (ImGui::MenuItem("Reset All Options"))
			{
				options::ResetAllOptions();
				Restart();
			}
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu(pb::get_rc_string(Msg::Menu1_Help)))
		{
#ifndef NDEBUG
			if (ImGui::MenuItem("ImGui Demo", nullptr, ShowImGuiDemo))
			{
				ShowImGuiDemo ^= true;
			}
#endif
			if (ImGui::MenuItem("Sprite Viewer", nullptr, ShowSpriteViewer))
			{
				if (!ShowSpriteViewer)
					pause(false);
				ShowSpriteViewer ^= true;
			}
			if (pb::cheat_mode && ImGui::MenuItem("Frame Times", nullptr, DispGRhistory))
			{
				DispGRhistory ^= true;
			}
			if (ImGui::MenuItem("Debug Overlay", nullptr, Options.DebugOverlay))
			{
				Options.DebugOverlay ^= true;
			}
			if (Options.DebugOverlay && ImGui::BeginMenu("Overlay Options"))
			{
				if (ImGui::MenuItem("Box Grid", nullptr, Options.DebugOverlayGrid))
					Options.DebugOverlayGrid ^= true;
				if (ImGui::MenuItem("Ball Depth Grid", nullptr, Options.DebugOverlayBallDepthGrid))
					Options.DebugOverlayBallDepthGrid ^= true;
				if (ImGui::MenuItem("Sprite Positions", nullptr, Options.DebugOverlaySprites))
					Options.DebugOverlaySprites ^= true;
				if (ImGui::MenuItem("All Edges", nullptr, Options.DebugOverlayAllEdges))
					Options.DebugOverlayAllEdges ^= true;
				if (ImGui::MenuItem("Component AABB", nullptr, Options.DebugOverlayAabb))
					Options.DebugOverlayAabb ^= true;
				if (ImGui::MenuItem("Ball Position", nullptr, Options.DebugOverlayBallPosition))
					Options.DebugOverlayBallPosition ^= true;
				if (ImGui::MenuItem("Ball Box Edges", nullptr, Options.DebugOverlayBallEdges))
					Options.DebugOverlayBallEdges ^= true;
				if (ImGui::MenuItem("Sound Positions", nullptr, Options.DebugOverlaySounds))
					Options.DebugOverlaySounds ^= true;
				if (ImGui::MenuItem("Apply Collision Mask", nullptr, Options.DebugOverlayCollisionMask))
					Options.DebugOverlayCollisionMask ^= true;
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu("Cheats"))
			{
				if (ImGui::MenuItem("hidden test", nullptr, pb::cheat_mode))
					pb::PushCheat("hidden test");
				if (ImGui::MenuItem("1max"))
					pb::PushCheat("1max");
				if (ImGui::MenuItem("bmax", nullptr, control::table_unlimited_balls))
					pb::PushCheat("bmax");
				if (ImGui::MenuItem("gmax"))
					pb::PushCheat("gmax");
				if (ImGui::MenuItem("rmax"))
					pb::PushCheat("rmax");
				if (pb::FullTiltMode && ImGui::MenuItem("quote"))
					pb::PushCheat("quote");
				if (ImGui::MenuItem("easy mode", nullptr, control::easyMode))
					pb::PushCheat("easy mode");

				ImGui::EndMenu();
			}
			ImGui::Separator();

			if (ImGui::MenuItem(pb::get_rc_string(Msg::Menu1_About_Pinball)))
			{
				pause(false);
				ShowAboutDialog = true;
			}
			ImGui::EndMenu();
		}
		if (VrMode)
			VrMenu();
		if (DispFrameRate && !FpsDetails.empty())
			if (ImGui::BeginMenu(FpsDetails.c_str()))
				ImGui::EndMenu();
		ImGui::EndMainMenuBar();
	}

	a_dialog();
	high_score::RenderHighScoreDialog();
	font_selection::RenderDialog();
	if (ShowSpriteViewer)
		render::SpriteViewer(&ShowSpriteViewer);
	options::RenderControlDialog();
	if (DispGRhistory)
		RenderFrameTimeDialog();

	const auto exitText = translations::GetTranslation(Msg::Menu1_Exit);
	if (ShowExitPopup)
	{
		ShowExitPopup = false;
		pause(false);
		ImGui::OpenPopup(exitText);
		ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	}
	if (ImGui::BeginPopupModal(exitText, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::Text("Exit the game?");
		ImGui::Separator();

		if (ImGui::Button(pb::get_rc_string(Msg::GenericOk), ImVec2(120, 0)))
		{
			SDL_Event event{SDL_QUIT};
			SDL_PushEvent(&event);
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::IsWindowAppearing())
		{
			ImGui::SetKeyboardFocusHere(0);
		}
		if (ImGui::Button(pb::get_rc_string(Msg::GenericCancel), ImVec2(120, 0)))
		{
			end_pause();
			ImGui::CloseCurrentPopup();
		}
		ImGui::SetItemDefaultFocus();
		ImGui::EndPopup();
	}

	// Print game texts on the sidebar
	gdrv::grtext_draw_ttext_in_box();
}

int winmain::event_handler(const SDL_Event* event)
{
	auto inputDown = false;
	switch (event->type)
	{
	case SDL_KEYDOWN:
	case SDL_MOUSEBUTTONDOWN:
	case SDL_CONTROLLERBUTTONDOWN:
		inputDown = true;
		break;
	default: break;
	}
	if (!options::WaitingForInput() || !inputDown)
		ImGui_ImplSDL2_ProcessEvent(event);

	bool mouseEvent;
	switch (event->type)
	{
	case SDL_MOUSEMOTION:
	case SDL_MOUSEBUTTONDOWN:
	case SDL_MOUSEBUTTONUP:
	case SDL_MOUSEWHEEL:
		CursorIdleCounter = 1000;
		mouseEvent = true;
		break;
	default:
		mouseEvent = false;
		break;
	}

	if (ImIO->WantCaptureMouse && !options::WaitingForInput())
	{
		if (mouse_down)
		{
			mouse_down = 0;
			SDL_SetWindowGrab(MainWindow, SDL_FALSE);
		}
		if (mouseEvent)
			return 1;
	}
	if (ImIO->WantCaptureKeyboard && !options::WaitingForInput())
	{
		switch (event->type)
		{
		case SDL_KEYDOWN:
		case SDL_KEYUP:
		case SDL_CONTROLLERBUTTONDOWN:
		case SDL_CONTROLLERBUTTONUP:
			return 1;
		default: ;
		}
	}

	switch (event->type)
	{
	case SDL_QUIT:
		end_pause();
		bQuit = true;
		fullscrn::shutdown();
		return_value = 0;
		return 0;
	case SDL_KEYUP:
		pb::InputUp({InputTypes::Keyboard, event->key.keysym.sym});
		break;
	case SDL_KEYDOWN:
		if (event->key.repeat)
			break;

		pb::InputDown({InputTypes::Keyboard, event->key.keysym.sym});
		if (!pb::cheat_mode)
			break;

		switch (event->key.keysym.sym)
		{
		case SDLK_g:
			DispGRhistory ^= true;
			break;
		case SDLK_o:
			{
				auto plt = new ColorRgba[4 * 256];
				auto pltPtr = &plt[10]; // first 10 entries are system colors hardcoded in display_palette()
				for (int i1 = 0, i2 = 0; i1 < 256 - 10; ++i1, i2 += 8)
				{
					unsigned char blue = i2, redGreen = i2;
					if (i2 > 255)
					{
						blue = 255;
						redGreen = i1;
					}

					*pltPtr++ = ColorRgba{blue, redGreen, redGreen, 0};
				}
				gdrv::display_palette(plt);
				delete[] plt;
			}
			break;
		case SDLK_y:
			SDL_SetWindowTitle(MainWindow, "Pinball");
			DispFrameRate ^= true;
			break;
		case SDLK_F1:
			pb::frame(10);
			break;
		case SDLK_F10:
			single_step ^= true;
			if (!single_step)
				no_time_loss = true;
			break;
		default:
			break;
		}
		break;
	case SDL_MOUSEBUTTONDOWN:
		{
			bool noInput = false;
			switch (event->button.button)
			{
			case SDL_BUTTON_LEFT:
				if (pb::cheat_mode)
				{
					mouse_down = 1;
					last_mouse_x = event->button.x;
					last_mouse_y = event->button.y;
					SDL_SetWindowGrab(MainWindow, SDL_TRUE);
					noInput = true;
				}
				break;
			default:
				break;
			}

			if (!noInput)
				pb::InputDown({InputTypes::Mouse, event->button.button});
		}
		break;
	case SDL_MOUSEBUTTONUP:
		{
			bool noInput = false;
			switch (event->button.button)
			{
			case SDL_BUTTON_LEFT:
				if (mouse_down)
				{
					mouse_down = 0;
					SDL_SetWindowGrab(MainWindow, SDL_FALSE);
					noInput = true;
				}
				break;
			default:
				break;
			}

			if (!noInput)
				pb::InputUp({InputTypes::Mouse, event->button.button});
		}
		break;
	case SDL_WINDOWEVENT:
		if (VrMode)
			break;
		switch (event->window.event)
		{
		case SDL_WINDOWEVENT_FOCUS_GAINED:
		case SDL_WINDOWEVENT_TAKE_FOCUS:
		case SDL_WINDOWEVENT_SHOWN:
			activated = true;
			Sound::Activate();
			if (Options.Music && !single_step)
				midi::music_play();
			no_time_loss = true;
			has_focus = true;
			break;
		case SDL_WINDOWEVENT_FOCUS_LOST:
		case SDL_WINDOWEVENT_HIDDEN:
			activated = false;
			fullscrn::activate(0);
			Options.FullScreen = false;
			Sound::Deactivate();
			midi::music_stop();
			has_focus = false;
			pb::loose_focus();
			break;
		case SDL_WINDOWEVENT_SIZE_CHANGED:
		case SDL_WINDOWEVENT_RESIZED:
			fullscrn::window_size_changed();
			break;
		default: ;
		}
		break;
	case SDL_JOYDEVICEADDED:
		if (SDL_IsGameController(event->jdevice.which))
		{
			SDL_GameControllerOpen(event->jdevice.which);
		}
		break;
	case SDL_JOYDEVICEREMOVED:
		{
			SDL_GameController* controller = SDL_GameControllerFromInstanceID(event->jdevice.which);
			if (controller)
			{
				SDL_GameControllerClose(controller);
			}
		}
		break;
	case SDL_CONTROLLERBUTTONDOWN:
		pb::InputDown({InputTypes::GameController, event->cbutton.button});
		break;
	case SDL_CONTROLLERBUTTONUP:
		pb::InputUp({InputTypes::GameController, event->cbutton.button});
		break;
	case SDL_CONTROLLERAXISMOTION:
		// Analog triggers mirror the shoulder buttons, so LT/RT (L2/R2) work as flippers too.
		if (event->caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT ||
			event->caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT)
		{
			const bool left = event->caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT;
			auto& state = ControllerTriggerState[left ? 0 : 1];
			const int button = left ? SDL_CONTROLLER_BUTTON_LEFTSHOULDER : SDL_CONTROLLER_BUTTON_RIGHTSHOULDER;
			if (!state && event->caxis.value > 16000)
			{
				state = 1;
				pb::InputDown({InputTypes::GameController, button});
			}
			else if (state && event->caxis.value < 8000)
			{
				state = 0;
				pb::InputUp({InputTypes::GameController, button});
			}
		}
		break;
	default: ;
	}

	return 1;
}

int winmain::ProcessWindowMessages()
{
	static auto idleWait = 0;
	SDL_Event event;
	if (has_focus)
	{
		idleWait = static_cast<int>(TargetFrameTime.count());
		while (SDL_PollEvent(&event))
		{
			if (!event_handler(&event))
				return 0;
		}

		return 1;
	}

	// Progressively wait longer when transitioning to idle
	idleWait = std::min(idleWait + static_cast<int>(TargetFrameTime.count()), 500);
	if (SDL_WaitEventTimeout(&event, idleWait))
	{
		idleWait = static_cast<int>(TargetFrameTime.count());
		return event_handler(&event);
	}
	return 1;
}

void winmain::memalloc_failure()
{
	midi::music_stop();
	Sound::Close();
	const char* caption = pb::get_rc_string(Msg::STRING270);
	const char* text = pb::get_rc_string(Msg::STRING279);
	pb::ShowMessageBox(SDL_MESSAGEBOX_ERROR, caption, text);
	std::exit(1);
}

void winmain::a_dialog()
{
	if (ShowAboutDialog == true)
	{
		ShowAboutDialog = false;
		ImGui::OpenPopup(pb::get_rc_string(Msg::STRING204));
	}

	bool unused_open = true;
	ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2{ 600, 300 });
	if (ImGui::BeginPopupModal(pb::get_rc_string(Msg::STRING204), &unused_open, ImGuiWindowFlags_None))
	{
		if (ImGui::BeginTabBar("AboutTabBar", ImGuiTabBarFlags_None))
		{
			if (ImGui::BeginTabItem("3DPB"))
			{
				ImGui::TextUnformatted(pb::get_rc_string(Msg::STRING139));
				ImGui::TextUnformatted("Original game by Cinematronics, Microsoft");
				ImGui::Separator();

				ImGui::TextUnformatted("Decompiled -> Ported to SDL");
				ImGui::Text("Version %s", Version);
				if (ImGui::SmallButton("Project home: https://github.com/k4zmu2a/SpaceCadetPinball"))
				{
#if SDL_VERSION_ATLEAST(2, 0, 14)
					// Relatively new feature, skip with older SDL
					SDL_OpenURL("https://github.com/k4zmu2a/SpaceCadetPinball");
#endif
				}
				ImGui::EndTabItem();
			}
			ImGui::PushStyleColor(ImGuiCol_Button, 0);
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, 0);
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, 0);
			if (ImGui::BeginTabItem("Full Tilt!"))
			{
				const ImVec2 buttonCenter = { -1, 0 };
				ImGui::Button("Full Tilt! was created by Cinematronics for Maxis.", buttonCenter);
				ImGui::Button("Version 1.1", buttonCenter);

				auto tableRow = [](LPCSTR textA, LPCSTR textB)
				{
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					ImGui::TextUnformatted(textA);
					ImGui::TableNextColumn();
					ImGui::TextUnformatted(textB);
				};
				if (ImGui::BeginTable("Full Tilt!", 2))
				{
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					ImGui::Button("Cinematronics", buttonCenter);
					ImGui::Separator();
					if (ImGui::BeginTable("Cinematronics", 2))
					{
						tableRow("PROGRAMMING", "ART");
						tableRow("Michael Sandige", "John Frantz");
						tableRow("John Taylor", "Ryan Medeiros");
						ImGui::EndTable();
					}
					ImGui::Separator();
					if (ImGui::BeginTable("Cinematronics", 2))
					{
						tableRow("DESIGN", "SOUND EFFECTS");
						tableRow("Kevin Gliner", "Matt Ridgeway");
						tableRow(nullptr, "Donald S. Griffin");
						ImGui::EndTable();
					}
					ImGui::Separator();
					if (ImGui::BeginTable("Cinematronics", 2))
					{
						tableRow("DESIGN CONSULTANT", "MUSIC");
						tableRow("Mark Sprenger", "Matt Ridgeway");
						ImGui::EndTable();
					}
					ImGui::Separator();
					if (ImGui::BeginTable("Cinematronics", 2))
					{
						tableRow("PRODUCER", "VOICES");
						tableRow("Kevin Gliner", "Mike McGeary");
						tableRow(nullptr, "William Rice");
						ImGui::EndTable();
					}
					ImGui::Separator();
					if (ImGui::BeginTable("Cinematronics", 2))
					{
						tableRow("GRAND POOBAH", nullptr);
						tableRow("David Stafford", nullptr);
						ImGui::EndTable();
					}
					ImGui::Separator();
					ImGui::Button("SPECIAL THANKS", buttonCenter);
					if (ImGui::BeginTable("Cinematronics", 2))
					{
						tableRow("Paula Sandige", "Alex St. John");
						tableRow("Brad Silverberg", "Jeff Camp");
						tableRow("Danny Thorpe", "Greg Hospelhorn");
						tableRow("Keith Johnson", "Sean Grant");
						tableRow("Bob McAnn", "Michael Kelley");
						tableRow("Rob Rosenhouse", "Lisa Acton");
						ImGui::EndTable();
					}
					ImGui::TextUnformatted("Dan and Mitchell Roth");

					ImGui::TableNextColumn();
					ImGui::Button("Maxis", buttonCenter);
					ImGui::Separator();
					if (ImGui::BeginTable("Maxis", 2))
					{
						tableRow("PRODUCER", "PRODUCT MANAGER");
						tableRow("John Csicsery", "Larry Lee");
						ImGui::EndTable();
					}
					ImGui::Separator();
					if (ImGui::BeginTable("Maxis", 2))
					{
						tableRow("LEAD TESTER", "QA MANAGER");
						tableRow("Scott Shicoff", "Scott Shicoff");
						ImGui::EndTable();
					}
					ImGui::Separator();
					ImGui::Button("ADDITIONAL TESTING", buttonCenter);
					if (ImGui::BeginTable("Maxis", 2))
					{
						tableRow("Cathy Castro", "Robin Hines");
						tableRow("John \"Jussi\" Ylinen", "Keith Meyer");
						tableRow("Marc Meyer", "Owen Nelson");
						tableRow("Joe Longworth", "Peter Saylor");
						tableRow("Michael Gilmartin", "Robin Hines");
						ImGui::EndTable();
					}
					ImGui::Separator();
					if (ImGui::BeginTable("Maxis", 2))
					{
						tableRow("ADDITIONAL ART", "ART DIRECTOR");
						tableRow("Ocean Quigley", "Sharon Barr");
						tableRow("Rick Macaraeg", "INSTALL PROGRAM");
						tableRow("Charlie Aquilina", "Kevin O'Hare");
						ImGui::EndTable();
					}
					ImGui::Separator();
					if (ImGui::BeginTable("Maxis", 2))
					{
						tableRow("INTRO MUSIC", "DOCUMENTATION");
						tableRow("Brian Conrad", "David Caggiano");
						tableRow("John Csicsery", "Michael Bremer");
						tableRow(nullptr, "Bob Sombrio");
						ImGui::EndTable();
					}
					ImGui::Separator();
					ImGui::Button("SPECIAL THANKS", buttonCenter);
					if (ImGui::BeginTable("Maxis", 2))
					{
						tableRow("Sam Poole", "Joe Scirica");
						tableRow("Jeff Braun", "Bob Derber");
						tableRow("Ashley Csicsery", "Tom Forge");
						ImGui::EndTable();
					}
					ImGui::Button("Will \"Burr\" Wright", buttonCenter);
					ImGui::EndTable();
				}

				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
			ImGui::PopStyleColor(3);
		}

		ImGui::Separator();
		if (ImGui::Button("Ok"))
		{
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
	ImGui::PopStyleVar();
}

void winmain::end_pause()
{
	if (single_step)
	{
		pb::pause_continue();
		no_time_loss = true;
	}
}

void winmain::new_game()
{
	end_pause();
	pb::replay_level(false);
}

void winmain::pause(bool toggle)
{
	if (toggle || !single_step)
	{
		pb::pause_continue();
		no_time_loss = true;
	}
}

void winmain::Restart()
{
	restart = true;
	SDL_Event event{SDL_QUIT};
	SDL_PushEvent(&event);
}

void winmain::UpdateFrameRate()
{
	// UPS >= FPS
	auto fps = Options.FramesPerSecond.V, ups = Options.UpdatesPerSecond.V;
	UpdateToFrameRatio = static_cast<double>(ups) / fps;
	TargetFrameTime = DurationMs(1000.0 / ups);
}

void winmain::HandleGameBinding(GameBindings binding, bool shortcut)
{
	switch (binding)
	{
	case GameBindings::TogglePause:
		pause();
		break;
	case GameBindings::NewGame:
		new_game();
		break;
	case GameBindings::ToggleFullScreen:
		options::toggle(Menu1::Full_Screen);
		break;
	case GameBindings::ToggleSounds:
		options::toggle(Menu1::Sounds);
		break;
	case GameBindings::ToggleMusic:
		options::toggle(Menu1::Music);
		break;
	case GameBindings::ShowControlDialog:
		pause(false);
		options::ShowControlDialog();
		break;
	case GameBindings::ToggleMenuDisplay:
		options::toggle(Menu1::Show_Menu);
		break;
	case GameBindings::Exit:
		if (!shortcut)
		{
			SDL_Event event{SDL_QUIT};
			SDL_PushEvent(&event);
		}
		else
			ShowExitPopup = true;
		break;
	default:
		break;
	}
}

void winmain::RenderFrameTimeDialog()
{
	ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2{300, 70});
	if (ImGui::Begin("Frame Times", &DispGRhistory, ImGuiWindowFlags_NoScrollbar))
	{
		auto target = static_cast<float>(TargetFrameTime.count());
		auto yMax = target * 2;

		auto spin = Options.HybridSleep ? static_cast<float>(SpinThreshold.count()) : 0;
		ImGui::Text("YMin:0ms, Target frame time:%03.04fms, YMax:%03.04fms, SpinThreshold:%03.04fms",
		            target, yMax, spin);

		static bool scrollPlot = true;
		ImGui::Checkbox("Scroll Plot", &scrollPlot);

		ImGui::SameLine();
		ImGui::SliderFloat("Window Size", &gfrWindow, 0.1f, 15, "%.3fsec", ImGuiSliderFlags_AlwaysClamp);

		{
			float average = 0.0f, dev = 0.0f;
			for (auto n : gfrDisplay)
			{
				average += n;
				dev += std::abs(target - n);
			}
			average /= static_cast<float>(gfrDisplay.size());
			dev /= static_cast<float>(gfrDisplay.size());
			char overlay[64];
			sprintf(overlay, "avg %.3fms, dev %.3fms", average, dev);

			auto region = ImGui::GetContentRegionAvail();
			ImGui::PlotLines("Lines", gfrDisplay.data(), static_cast<int>(gfrDisplay.size()),
			                 scrollPlot ? gfrOffset : 0, overlay, 0, yMax, region);
		}
	}
	ImGui::End();
	ImGui::PopStyleVar();
}

void winmain::HybridSleep(DurationMs sleepTarget)
{
	static constexpr double StdDevFactor = 0.5;

	// This nice concept is from https://blat-blatnik.github.io/computerBear/making-accurate-sleep-function/
	// Sacrifices some CPU time for smaller frame time jitter
	while (sleepTarget > SpinThreshold)
	{
		auto start = Clock::now();
		std::this_thread::sleep_for(DurationMs(1));
		auto end = Clock::now();

		auto actualDuration = DurationMs(end - start);
		sleepTarget -= actualDuration;

		// Update expected sleep duration using Welford's online algorithm
		// With bad timer, this will run away to 100% spin
		SleepState.Advance(actualDuration.count());
		SpinThreshold = DurationMs(SleepState.mean + SleepState.GetStdDev() * StdDevFactor);
	}

	// spin lock
	for (auto start = Clock::now(); DurationMs(Clock::now() - start) < sleepTarget;);
}

void winmain::ImGuiMenuItemWShortcut(GameBindings binding, bool selected)
{
	const auto& keyDef = Options.Key[~binding];
	if (ImGui::MenuItem(pb::get_rc_string(keyDef.Description), keyDef.GetShortcutDescription().c_str(), selected))
	{
		HandleGameBinding(binding, false);
	}
}

// ============================================================================ VR mode

bool winmain::VrUiVisible()
{
	return Options.ShowMenu || options::IsControlDialogShown() || ShowAboutDialog || ShowExitPopup ||
		ShowSpriteViewer || DispGRhistory || ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
}

void winmain::VrSyncSettings(bool toVr)
{
#ifdef SCP_VR
	auto s = vr::GetSettings();
	if (toVr)
	{
		s.Mode = Options.VrViewMode.V == 1 ? vr::ViewMode::Theater : vr::ViewMode::Cabinet;
		s.TiltDeg = Clamp(Options.VrTableTilt.V, 6.0f, 70.0f);
		s.Distance = Clamp(Options.VrTableDistance.V, 0.05f, 1.2f);
		s.HeightOffset = Clamp(Options.VrTableHeight.V, -0.6f, 0.6f);
		s.TableScale = Clamp(Options.VrTableScale.V, 0.6f, 1.6f);
		s.TheaterWidth = Clamp(Options.VrTheaterWidth.V, 1.0f, 6.0f);
		s.RenderScale = VrRenderScale;
		vr::SetSettings(s);
	}
	else
	{
		Options.VrViewMode.V = s.Mode == vr::ViewMode::Theater ? 1 : 0;
		Options.VrTableTilt.V = s.TiltDeg;
		Options.VrTableDistance.V = s.Distance;
		Options.VrTableHeight.V = s.HeightOffset;
		Options.VrTableScale.V = s.TableScale;
		Options.VrTheaterWidth.V = s.TheaterWidth;
	}
#endif
}

void winmain::VrMenu()
{
#ifdef SCP_VR
	if (!ImGui::BeginMenu("VR"))
		return;
	auto s = vr::GetSettings();
	bool changed = false;
	bool theater = s.Mode == vr::ViewMode::Theater;
	if (ImGui::MenuItem("Cabinet view", nullptr, !theater))
	{
		s.Mode = vr::ViewMode::Cabinet;
		changed = true;
	}
	if (ImGui::MenuItem("Theater screen", nullptr, theater))
	{
		s.Mode = vr::ViewMode::Theater;
		changed = true;
	}
	ImGui::Separator();
	ImGui::PushItemWidth(160 * FontSizeMultiplier);
	changed |= ImGui::SliderFloat("Playfield tilt", &s.TiltDeg, 6.0f, 70.0f, "%.0f deg");
	changed |= ImGui::SliderFloat("Distance", &s.Distance, 0.05f, 1.2f, "%.2f m");
	changed |= ImGui::SliderFloat("Height", &s.HeightOffset, -0.6f, 0.6f, "%+.2f m");
	changed |= ImGui::SliderFloat("Table size", &s.TableScale, 0.6f, 1.6f, "%.2fx");
	changed |= ImGui::SliderFloat("Theater width", &s.TheaterWidth, 1.0f, 6.0f, "%.1f m");
	ImGui::PopItemWidth();
	if (ImGui::MenuItem("Recenter view"))
		vr::RequestRecenter();
	if (ImGui::MenuItem("Reset table placement"))
	{
		vr::Settings def;
		s.TiltDeg = def.TiltDeg;
		s.Distance = def.Distance;
		s.HeightOffset = def.HeightOffset;
		s.TableScale = def.TableScale;
		s.TheaterWidth = def.TheaterWidth;
		changed = true;
	}
	ImGui::Separator();
	if (ImGui::BeginMenu("Controls"))
	{
		ImGui::TextUnformatted("Triggers / bumpers   Flippers");
		ImGui::TextUnformatted("A                    Plunger (hold, release)");
		ImGui::TextUnformatted("Left stick / D-pad   Nudge left, right, up");
		ImGui::TextUnformatted("B                    Pause");
		ImGui::TextUnformatted("X (hold)             New game");
		ImGui::TextUnformatted("Y                    Cabinet / theater view");
		ImGui::TextUnformatted("Right stick          Move table, change tilt");
		ImGui::TextUnformatted("Right stick click    Recenter");
		ImGui::TextUnformatted("Menu / View          This menu");
		ImGui::TextUnformatted("In menus: point and pull a trigger.");
		ImGui::Separator();
		ImGui::Text("Controller profile: %s", vr::InteractionProfileName());
		ImGui::EndMenu();
	}
	if (changed)
		vr::SetSettings(s);
	ImGui::EndMenu();
#endif
}

#ifdef SCP_VR
// ---------------------------------------------------------------------------- VR artwork layout
// The table bitmap is a perspective rendering of a flat table. The game keeps the camera it was rendered
// with (proj), so the VR view can map a flat rectangle back onto the image and let VR provide the
// perspective instead. The edges of the painted metal frames are detected from the image itself.
namespace
{
	struct VrArtwork
	{
		bool Valid = false;
		float WorldLeftX = 0, WorldRightX = 0, WorldNearY = 0, WorldFarY = 0;
		float SideMin[2] = {0, 0}, SideMax[2] = {0, 0}; // score panel bounds, vscreen pixels
		SDL_Rect Dest{};
		int SurfW = 1, SurfH = 1, VW = 1, VH = 1;
		gdrv_bitmap8* Screen = nullptr;
		int Frames = 0;
	} VrArt;

	bool VrBright(const gdrv_bitmap8* bmp, int x, int y)
	{
		auto c = bmp->BmpBufPtr1[y * bmp->Stride + x];
		return std::max({c.GetRed(), c.GetGreen(), c.GetBlue()}) > 24;
	}

	void VrPlayfieldMap(float s, float t, float* u, float* v)
	{
		const auto& a = VrArt;
		vector3 pt{a.WorldLeftX + (a.WorldRightX - a.WorldLeftX) * s, a.WorldNearY + (a.WorldFarY - a.WorldNearY) * t, 0};
		auto p = proj::xform_to_2d_f(pt);
		*u = (a.Dest.x + p.X * a.Dest.w / a.VW) / a.SurfW;
		*v = (a.Dest.y + p.Y * a.Dest.h / a.VH) / a.SurfH;
	}
}

void winmain::VrUpdateArtworkLayout(vr::FrameLayout& layout, int surfW, int surfH)
{
	auto bmp = render::vscreen;
	if (!pb::MainTable || !bmp || bmp->Width <= 0 || !bmp->BmpBufPtr1)
		return;
	auto& a = VrArt;
	const auto& d = render::DestinationRect;
	if (!a.Valid || a.Screen != bmp || a.Dest.w != d.w || a.Dest.h != d.h || a.Dest.x != d.x || a.Dest.y != d.y ||
		++a.Frames % 300 == 0)
	{
		a.Screen = bmp;
		a.Dest = d;
		a.SurfW = surfW;
		a.SurfH = surfH;
		a.VW = bmp->Width;
		a.VH = bmp->Height;
		const int boardW = std::min(pb::MainTable->Width, bmp->Width);

		// Playfield frame: left/right on the bottom row, top edge in the middle column.
		int xl = 0, xr = boardW, yt = 0;
		const int yb = bmp->Height - 1;
		while (xl < boardW - 1 && !VrBright(bmp, xl, yb)) xl++;
		while (xr > xl + 1 && !VrBright(bmp, xr - 1, yb)) xr--;
		while (yt < bmp->Height / 2 && !VrBright(bmp, boardW / 2, yt)) yt++;
		if (xr - xl < boardW / 2)
		{
			xl = 0;
			xr = boardW;
		}
		auto wl = proj::ReverseXFormF(static_cast<float>(xl), static_cast<float>(bmp->Height));
		auto wr = proj::ReverseXFormF(static_cast<float>(xr), static_cast<float>(bmp->Height));
		auto wt = proj::ReverseXFormF(boardW * 0.5f, static_cast<float>(yt));
		a.WorldLeftX = wl.X;
		a.WorldRightX = wr.X;
		a.WorldNearY = wl.Y;
		a.WorldFarY = wt.Y;

		// Score panel: bounding box of the non-black artwork right of the table.
		int minX = bmp->Width, minY = bmp->Height, maxX = -1, maxY = -1;
		for (int y = 0; y < bmp->Height; y++)
			for (int x = boardW; x < bmp->Width; x++)
				if (VrBright(bmp, x, y))
				{
					minX = std::min(minX, x);
					maxX = std::max(maxX, x);
					minY = std::min(minY, y);
					maxY = std::max(maxY, y);
				}
		if (maxX > minX && maxY > minY)
		{
			a.SideMin[0] = static_cast<float>(minX);
			a.SideMin[1] = static_cast<float>(minY);
			a.SideMax[0] = static_cast<float>(maxX + 1);
			a.SideMax[1] = static_cast<float>(maxY + 1);
		}
		else
		{
			a.SideMin[0] = static_cast<float>(boardW);
			a.SideMin[1] = 0;
			a.SideMax[0] = static_cast<float>(bmp->Width);
			a.SideMax[1] = static_cast<float>(bmp->Height);
		}
		const float width = std::fabs(a.WorldRightX - a.WorldLeftX);
		a.Valid = width > 1e-3f && std::fabs(a.WorldFarY - a.WorldNearY) > 1e-3f;
	}
	if (!a.Valid)
		return;

	layout.PlayfieldMap = VrPlayfieldMap;
	layout.PlayfieldAspect = std::fabs(a.WorldFarY - a.WorldNearY) / std::fabs(a.WorldRightX - a.WorldLeftX);
	layout.Sidebar[0] = (a.Dest.x + a.SideMin[0] * a.Dest.w / a.VW) / surfW;
	layout.Sidebar[1] = (a.Dest.y + a.SideMin[1] * a.Dest.h / a.VH) / surfH;
	layout.Sidebar[2] = (a.Dest.x + a.SideMax[0] * a.Dest.w / a.VW) / surfW;
	layout.Sidebar[3] = (a.Dest.y + a.SideMax[1] * a.Dest.h / a.VH) / surfH;
}
#endif

void winmain::VrMainLoop()
{
#ifdef SCP_VR
	bQuit = false;
	auto prevTime = Clock::now();
	DurationMs accumulator(0);
	vr::Input prev{};
	bool prevUi = false;
	bool focusPaused = false;

	auto setBinding = [](GameBindings binding, bool down, bool wasDown)
	{
		if (down == wasDown)
			return;
		if (down)
			pb::BindingDown(binding);
		else
			pb::BindingUp(binding);
	};

	auto closeVrMenu = [&]()
	{
		if (Options.ShowMenu)
			options::toggle(Menu1::Show_Menu);
		if (VrMenuPaused)
		{
			end_pause();
			VrMenuPaused = false;
		}
	};

	while (true)
	{
		if (!ProcessWindowMessages() || bQuit)
			break;
		if (!vr::PollEvents())
		{
			bQuit = true;
			break;
		}
		if (!vr::IsSessionRunning())
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(20));
			prevTime = Clock::now();
			continue;
		}
		if (!vr::BeginFrame())
			continue;

		has_focus = true;
		const bool ui = VrUiVisible();
		vr::Input in;
		vr::SyncInput(in, ui);

		// Pause while the headset is off or a system overlay has focus.
		const bool focused = vr::IsSessionFocused();
		if (!focused && !focusPaused)
		{
			focusPaused = true;
			if (!single_step)
			{
				pause(false);
				Sound::Deactivate();
			}
			else
				focusPaused = false;
		}
		else if (focused && focusPaused)
		{
			focusPaused = false;
			Sound::Activate();
			end_pause();
		}

		// --- game buttons
		if (ui && !prevUi)
		{
			// Release anything held when a menu opens.
			setBinding(GameBindings::LeftFlipper, false, prev.FlipperLeft);
			setBinding(GameBindings::RightFlipper, false, prev.FlipperRight);
			setBinding(GameBindings::Plunger, false, prev.Plunger);
			setBinding(GameBindings::LeftTableBump, false, prev.NudgeLeft);
			setBinding(GameBindings::RightTableBump, false, prev.NudgeRight);
			setBinding(GameBindings::BottomTableBump, false, prev.NudgeUp);
			prev = vr::Input{};
		}
		if (!ui && prevUi)
		{
			// A menu just closed: ignore buttons still held from clicking in it.
			prev = in;
		}
		else if (!ui)
		{
			if (in.FlipperLeft && !prev.FlipperLeft)
				vr::Haptic(vr::Hand::Left, 0.22f, 0.018f);
			if (in.FlipperRight && !prev.FlipperRight)
				vr::Haptic(vr::Hand::Right, 0.22f, 0.018f);
			if (in.NudgeLeft && !prev.NudgeLeft)
			{
				vr::ShakeTable(0.35f, 0);
				vr::HapticBoth(0.5f, 0.05f);
			}
			if (in.NudgeRight && !prev.NudgeRight)
			{
				vr::ShakeTable(-0.35f, 0);
				vr::HapticBoth(0.5f, 0.05f);
			}
			if (in.NudgeUp && !prev.NudgeUp)
			{
				vr::ShakeTable(0, -0.35f);
				vr::HapticBoth(0.5f, 0.05f);
			}
			setBinding(GameBindings::LeftFlipper, in.FlipperLeft, prev.FlipperLeft);
			setBinding(GameBindings::RightFlipper, in.FlipperRight, prev.FlipperRight);
			setBinding(GameBindings::Plunger, in.Plunger, prev.Plunger);
			setBinding(GameBindings::LeftTableBump, in.NudgeLeft, prev.NudgeLeft);
			setBinding(GameBindings::RightTableBump, in.NudgeRight, prev.NudgeRight);
			setBinding(GameBindings::BottomTableBump, in.NudgeUp, prev.NudgeUp);
			prev = in;
		}
		prevUi = ui;

		if (in.PausePressed && !ui)
			pause();
		if (in.NewGameTriggered)
		{
			closeVrMenu();
			new_game();
		}
		if (in.MenuPressed)
		{
			if (Options.ShowMenu)
				closeVrMenu();
			else
			{
				options::toggle(Menu1::Show_Menu);
				if (!single_step)
				{
					pause(false);
					VrMenuPaused = true;
				}
			}
		}
		if (in.BackPressed && Options.ShowMenu)
			closeVrMenu();
		if (!Options.ShowMenu && VrMenuPaused && !single_step)
			VrMenuPaused = false; // unpaused from the menu itself

		// --- simulation: fixed steps at the configured update rate
		auto now = Clock::now();
		auto frameTime = std::min(DurationMs(now - prevTime), DurationMs(100));
		prevTime = now;
		if (no_time_loss)
		{
			accumulator = DurationMs(0);
			no_time_loss = false;
		}
		if (!single_step)
		{
			accumulator += frameTime;
			int steps = 0;
			while (accumulator >= TargetFrameTime && steps < 16)
			{
				pb::frame(static_cast<float>(TargetFrameTime.count()));
				accumulator -= TargetFrameTime;
				steps++;
			}
			if (steps == 16)
				accumulator = DurationMs(0);
		}

		// --- compose the 2D frame (table, score panel, menus) on the CPU
		if (in.PointerValid)
			ImIO->AddMousePosEvent(in.PointerX, in.PointerY);
		else
			ImIO->AddMousePosEvent(-FLT_MAX, -FLT_MAX);
		ImIO->AddMouseButtonEvent(0, in.PointerDown && in.PointerValid);
		if (in.Scroll != 0)
			ImIO->AddMouseWheelEvent(0, in.Scroll);
		if (!ui)
			ImGui::SetMouseCursor(ImGuiMouseCursor_None);

		ImGui_ImplSDL2_NewFrame();
		ImGui_Render_NewFrame();
		ImGui::NewFrame();
		RenderUi();

		SDL_RenderClear(Renderer);
		SDL_RenderFillRect(Renderer, nullptr);
		render::PresentVScreen();
		ImGui::Render();
		ImGui_Render_RenderDrawData(ImGui::GetDrawData());
		SDL_RenderPresent(Renderer);

		// --- hand it to the VR renderer
		vr::FrameImage image;
		vr::FrameLayout layout;
		if (VrSurface)
		{
			SDL_LockSurface(VrSurface);
			image.Pixels = VrSurface->pixels;
			image.Width = VrSurface->w;
			image.Height = VrSurface->h;
			image.Pitch = VrSurface->pitch;

			const auto& d = render::DestinationRect;
			float coef = 375.0f / 600.0f;
			if (pb::MainTable && render::vscreen && render::vscreen->Width > 0)
				coef = static_cast<float>(pb::MainTable->Width) / render::vscreen->Width;
			const float W = static_cast<float>(image.Width), H = static_cast<float>(image.Height);
			const float x0 = d.x / W, x1 = (d.x + d.w) / W, xs = (d.x + d.w * coef) / W;
			const float y0 = d.y / H, y1 = (d.y + d.h) / H;
			const float board[4] = {x0, y0, xs, y1}, side[4] = {xs, y0, x1, y1}, game[4] = {x0, y0, x1, y1};
			memcpy(layout.Board, board, sizeof board);
			memcpy(layout.Sidebar, side, sizeof side);
			memcpy(layout.Game, game, sizeof game);
			VrUpdateArtworkLayout(layout, image.Width, image.Height);
		}
		vr::EndFrame(image, layout, VrUiVisible());
		if (VrSurface)
			SDL_UnlockSurface(VrSurface);

		auto sdlError = SDL_GetError();
		if (sdlError[0])
		{
			if (sdlError != PrevSdlError)
			{
				printf("SDL Error: %s\n", sdlError);
				PrevSdlError = sdlError;
			}
			SDL_ClearError();
		}
	}
#endif
}
