/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef ENGINE_CLIENT_ENUMS_H
#define ENGINE_CLIENT_ENUMS_H

enum
{
	NUM_DUMMIES = 2,
};

// Kinetix compat: the Kinetix botnet was written for an 8-slot dummy engine.
// The engine itself still only supports NUM_DUMMIES(2) real connections; the
// compat overloads in IClient report dummies 2..7 as never connected, so all
// MAX_DUMMIES-sized arrays/loops in the Kinetix components stay in bounds.
enum
{
	MAX_DUMMIES = 8,
};

#endif
