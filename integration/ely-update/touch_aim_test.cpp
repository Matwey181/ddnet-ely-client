#include <game/client/components/bestclient/hook_aim_activation.h>
#include <cassert>
int main()
{
 using ElyMobile::HookAimActive;
 assert(HookAimActive(true,true,true,0,false,false));
 assert(!HookAimActive(true,true,false,0,false,false));
 assert(HookAimActive(true,true,false,1,false,false));
 assert(!HookAimActive(true,true,false,-1,false,false));
 assert(!HookAimActive(false,true,true,1,true,true));
 assert(!HookAimActive(true,false,true,0,false,true));
 assert(HookAimActive(true,false,false,0,true,true));
 assert(!HookAimActive(true,false,false,0,true,false));
}
