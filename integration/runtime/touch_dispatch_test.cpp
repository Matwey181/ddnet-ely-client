#include <game/client/mobile_runtime.h>

#include <algorithm>
#include <cassert>
#include <iostream>
#include <vector>

struct CHandler
{
	bool m_Consume = false;
	bool m_RemoveFirst = false;
	bool m_Pressed = false;
	int m_Calls = 0;
	int m_Seen = -1;

	bool OnTouchState(std::vector<int> &Fingers)
	{
		++m_Calls;
		m_Seen = Fingers.size();
		m_Pressed = !Fingers.empty();
		if(m_RemoveFirst && !Fingers.empty())
			Fingers.erase(Fingers.begin());
		return m_Consume;
	}
};

int main()
{
	int Clears = 0;
	auto Clear = [&]() { ++Clears; };
	CHandler Controls{true};
	std::vector<CHandler *> Handlers{&Controls};
	std::vector<int> Fingers{1, 2, 3};
	ElyMobile::DispatchTouchState(Handlers, Fingers, Clear);
	assert(Controls.m_Pressed && Controls.m_Seen == 3);
	assert(Fingers.empty() && Clears == 1);
	// Releasing all fingers must clear held movement/jump/hook without a new tap.
	ElyMobile::DispatchTouchState(Handlers, Fingers, Clear);
	assert(!Controls.m_Pressed && Controls.m_Calls == 2);

	CHandler Overlay{true}; // Deliberately does not clear the vector.
	CHandler Below{true};
	Handlers = {&Overlay, &Below};
	Fingers = {7, 8};
	Clears = 0;
	ElyMobile::DispatchTouchState(Handlers, Fingers, Clear);
	assert(Overlay.m_Seen == 2 && Below.m_Seen == 0 && !Below.m_Pressed);
	assert(Clears == 1);

	CHandler Partial{false, true};
	CHandler Remaining{true};
	Handlers = {&Partial, &Remaining};
	Fingers = {10, 20};
	ElyMobile::DispatchTouchState(Handlers, Fingers, Clear);
	assert(Partial.m_Seen == 2 && Remaining.m_Seen == 1);
	Partial = CHandler{};
	Remaining = CHandler{};
	Fingers = {};
	Clears = 0;
	ElyMobile::DispatchTouchState(Handlers, Fingers, Clear);
	assert(Partial.m_Calls == 1 && Remaining.m_Calls == 1 && Clears == 0);

	int Render = 0, Update = 0, Inactive = 120;
	ElyMobile::ApplyFrameProfile(Render, Update, Inactive);
	assert(Render == 60 && Update == 120 && Inactive == 30);
	Render = 1000; Update = 1000; Inactive = 0;
	ElyMobile::ApplyFrameProfile(Render, Update, Inactive);
	assert(Render == 60 && Update == 120 && Inactive == 30);
	Render = 30; Update = 60; Inactive = 10;
	ElyMobile::ApplyFrameProfile(Render, Update, Inactive);
	assert(Render == 30 && Update == 60 && Inactive == 10);
	std::cout << "Touch release, overlay capture, partial touches and frame profile tests passed\n";
}
