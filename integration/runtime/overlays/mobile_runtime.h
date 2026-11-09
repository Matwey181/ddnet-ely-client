#ifndef GAME_CLIENT_MOBILE_RUNTIME_H
#define GAME_CLIENT_MOBILE_RUNTIME_H

namespace ElyMobile
{
// Empty frames carry touch releases. A consuming overlay must also release
// input in lower-priority components, including legacy components that forget
// to clear the shared finger vector themselves.
template<typename TComponents, typename TFingers, typename TClearDeltas>
void DispatchTouchState(TComponents &Components, TFingers &Fingers, TClearDeltas ClearDeltas)
{
	bool Used = false;
	for(auto *pComponent : Components)
	{
		if(pComponent->OnTouchState(Fingers))
		{
			Used = true;
			Fingers.clear();
		}
	}
	if(Used)
		ClearDeltas();
}

// One-time migration of the uncapped desktop profile on mobile. Existing
// lower limits and user-selected touch layouts are preserved.
inline void ApplyFrameProfile(int &RenderRate, int &UpdateRate, int &InactiveRate)
{
	if(RenderRate <= 0 || RenderRate > 60)
		RenderRate = 60;
	if(UpdateRate <= 0 || UpdateRate > 120)
		UpdateRate = 120;
	if(InactiveRate <= 0 || InactiveRate > 30)
		InactiveRate = 30;
}
}

#endif
