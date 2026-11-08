# Ely iOS port status

This is an integration of the complete BestClient tree at the commit in SOURCE_LOCK.json
with the mobile DDNet changes from sh1zoooo/ddnet_iOS. All BestClient modules and
assets are included. The generated `source` branch contains the full merged tree.

## Implemented mobile adapters

- Original touch controls, configurable movement joystick, relative aiming,
  compact button editor, custom button colors and image import.
- Separate Touch settings; legacy sh1zo/Kinetix settings page removed.
- On-screen push-to-talk and Fast Actions bindings.
- HUD editing through the existing touch-aware DDNet UI.
- Native Apple Music metadata, cover art and playback controls, with an explicit
  access button. Windows/Linux provider implementations remain in the source.
- Native ImageIO decoding for supported images and GIFs; native AVFoundation
  decoding for supported video formats and looping media backgrounds.
- Snake direction buttons and Minesweeper reveal/flag mode. Other mini-games
  retain their pointer/touch controls and existing on-screen buttons.
- Microphone, photo library, Apple Music and local-network usage descriptions.

## iOS restrictions and verification limits

- Other applications' music metadata and audio samples are not generally
  available through the public iOS APIs used here. Apple Music integration does
  not provide a live audio-spectrum capture from other apps.
- External process control (including changing Discord's process priority),
  desktop Discord RPC and desktop executable self-updates are not iOS features.
  App updates require replacing/re-signing the IPA.
- Native media support follows the formats supported by iOS, rather than every
  codec available in a desktop FFmpeg build.
- Voice, clans, translation, multimapping and client indicators still depend on
  the upstream services and their availability; mobile service interoperability
  requires an actual device test.
- Two unregistered experimental Lua source files in the iOS fork have no live
  CCodeExec instance and omit their Lua/sol2 dependencies. They are preserved in
  the source but excluded from compilation. All registered legacy components
  are retained.
- A successful compiler/package check does not establish device runtime or
  feature-by-feature test coverage. Do not describe this as a verified complete
  port until those tests have actually been performed.

The IPA is built for iPhoneOS arm64, with an iOS 15 minimum target, and is unsigned.
It requires signing before installation on a real device.
