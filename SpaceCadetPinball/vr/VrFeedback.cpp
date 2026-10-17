#include "VrFeedback.h"

#include "VrRuntime.h"

#include <cstring>

namespace VrFeedback
{
	static bool Starts(const char* s, const char* prefix)
	{
		return strncmp(s, prefix, strlen(prefix)) == 0;
	}

	void OnSound(const char* info)
	{
		if (!info || !vr::IsActive())
			return;

		if (Starts(info, "TPinballTable1")) // tilt
			vr::HapticBoth(1.0f, 0.35f);
		else if (Starts(info, "TKickback") || Starts(info, "TKickout2"))
			vr::HapticBoth(0.55f, 0.06f);
		else if (Starts(info, "TPlunger1")) // ball launched
			vr::Haptic(vr::Hand::Right, 0.7f, 0.08f);
		else if (Starts(info, "TCollisionComponent1") || Starts(info, "TBumper"))
			vr::HapticBoth(0.30f, 0.025f);
		else if (Starts(info, "TFlipper1"))
			vr::HapticBoth(0.18f, 0.02f);
		else if (Starts(info, "TSink") || Starts(info, "THole") || Starts(info, "TKickout1"))
			vr::HapticBoth(0.25f, 0.04f);
		else if (Starts(info, "TPopupTarget") || Starts(info, "TBlocker"))
			vr::HapticBoth(0.2f, 0.03f);
	}
}
