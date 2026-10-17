#pragma once
// Controller rumble driven by game events. The game tags every sound it plays with the
// component that caused it ("TBumper1", "TKickback", ...), which is a convenient, non-invasive
// place to derive haptic feedback from.

namespace VrFeedback
{
	void OnSound(const char* info);
}
