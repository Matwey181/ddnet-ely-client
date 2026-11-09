SoundVisualizer by Cagan Turgut (CaYatur) / CaYaDev
https://github.com/CaYatur/SoundVisualizer
Pinned revision: c3e7fc6bc3fa826e323486256268fa764a317963
License: MIT (full notice in LICENSE).

Adapted for Ely Client, not the original SoundVisualizer:
- src/shared/onset.js -> bestclient/ely_audio_analysis.h (C++ detector)
- Nebula and seeded RNG from backgrounds*.js -> bestclient/ely_nebula.h
- Canvas radial gradient and lighter composition -> cached native GPU glow rendering.
Ely changes: onset-gated envelopes, bounded aggregate brightness, reduced motion,
Windows WASAPI audio source, native DDNet rendering and user controls.
No SoundVisualizer Electron app, unrelated visualizers or third-party MilkDrop presets are embedded.
