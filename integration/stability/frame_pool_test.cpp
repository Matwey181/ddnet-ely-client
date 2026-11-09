#include <ios/ios_main.h>
#include <cassert>
#include <type_traits>
static int Live = 0, Created = 0;
void *IosFramePoolCreate() { ++Live; ++Created; return &Live; }
void IosFramePoolDrain(void *pPool) { assert(pPool == &Live); --Live; }
int main()
{
 static_assert(!std::is_copy_constructible_v<CIosFramePool>);
 for(int i = 0; i < 20000; ++i) { CIosFramePool Pool; assert(Live == 1); }
 assert(Live == 0 && Created == 20000);
 for(int i = 0; i < 3; ++i) { CIosFramePool Pool; if(i == 1) break; }
 assert(Live == 0);
 auto EarlyReturn = []() { CIosFramePool Pool; return 1; };
 assert(EarlyReturn() == 1 && Live == 0);
}
