# Native runtime design and acceptance

The target is original SM64DS gameplay on ARM64. Compilable source units,
initialized actors and a completed gameplay frame are separate milestones.
`CHECKPOINT.md` records which was actually exercised.

## Reference code inspected

| Area | Source evidence | Application to this port |
| --- | --- | --- |
| Frame ownership | [sm64-port pc_main.c](https://github.com/sm64-port/sm64-port/blob/master/src/pc/pc_main.c), `produce_one_frame` | Original game logic runs between graphics-frame boundaries. `StageFlow` keeps the DS cleanup/init/behavior/render/tree order; presentation must not replace behavior. |
| Native build | [Android base Gradle](https://github.com/VDavid003/sm64-port-android-base/blob/master/app/build.gradle), [Application.mk](https://github.com/VDavid003/sm64-port-android-base/blob/master/app/jni/Application.mk) | Compile original game code with a platform layer. This port uses its own ARM64 CMake target and audited projections. The base's old SDK settings are not imported. |
| Rendering | [sm64-port gfx_opengl.c](https://github.com/sm64-port/sm64-port/blob/master/src/pc/gfx/gfx_opengl.c) | Host GPU backend handles texture upload and triangle submission. DS GX commands, BMD/BCA/BTA records, palettes, VRAM capture and two-screen scanout retain DS semantics. N64 display-list translation cannot replace them. |
| Input | [Android touchscreen controller](https://github.com/VDavid003/sm64-port-android/blob/b43af4667bd1cde0d6cb11004add92ffaffab1c5/src/pc/controller/controller_touchscreen.c) | Touch contacts feed the game's controller state. This port merges Android touches/gamepads, then executes original DS input conversion and press/release logic each tick. |
| Collision | [sm64-port mario_step.c](https://github.com/sm64-port/sm64-port/blob/master/src/game/mario_step.c) | Movement consumes the original game's ground/wall queries. SM64DS retains fixed-point KCL octrees, surface filters, sphere/line queries and moving meshes; the N64 float collision algorithm is not interchangeable. |
| Audio | [Android SDL audio](https://github.com/VDavid003/sm64-port-android/blob/b43af4667bd1cde0d6cb11004add92ffaffab1c5/src/pc/audio/audio_sdl.c) and `pc_main.c` | Game logic and queued device playback have separate ownership. Android retains the pinned DS sequencer/mixer, bounded PCM queue and AAudio backend. |
| DS ABI | [pinned DS port README](https://github.com/tangosdev/sm64ds-decomp/blob/6230b2996636e3fd0e03cb178161c94cb4b73cc9/port/README.md), original actor/collision/model sources | That port explicitly starts with 32-bit hosts. Android therefore uses checked four-byte data/code handles and original member-pointer records; copying host-width C++ vtables into DS records is invalid. |

Missing game callbacks fail at their real call sites. An inherited original
empty method may stay empty; an unknown override must never report success.
Matching `src/` and `include/` are preserved. Resources come from the user's ROM,
not committed Nintendo assets.

## Required evidence before claiming playable

1. Select a real save through the original frontend and initialize scene 3.
2. Complete repeated original world frames, including Player state, actor lists,
   animation, camera, collision, rendering and sound updates.
3. Drive original controls: position changes for walking/running; jump rises,
   falls and lands; ground, wall and moving-object collision remain active.
4. Inspect rendered castle and character frames, with the camera following.
5. Build ARM64 and test the APK on Android, including pause/resume and controls.

An isolated 400-tick water-animation regression does not satisfy steps 2–5.
No attached Android device has been available in this session.
