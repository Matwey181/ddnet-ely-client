#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_HOOK_AIM_ACTIVATION_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_HOOK_AIM_ACTIVATION_H
namespace ElyMobile
{
inline bool HookAimActive(bool Ready, bool TouchControls, bool HookDown, int LatchedHook, bool KeyboardDown, bool ModifiersMatch)
{
 return Ready && ((TouchControls && (HookDown || LatchedHook > 0)) || (KeyboardDown && ModifiersMatch));
}
}
#endif
