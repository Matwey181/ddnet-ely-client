#ifndef IOS_PERFORMANCE_H
#define IOS_PERFORMANCE_H
#include <cstdint>
void IosPerfComponent(const char *pName, bool Render, int64_t Nanoseconds);
void IosPerfPhase(int Phase, int64_t Nanoseconds); // update, render, swap, event pump
void IosPerfFrame(float FrameSeconds);
void IosPerfShare();
#endif
