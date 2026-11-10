# The race camera (off limits)

**CLAUDE.md rule 7: do not change how the camera view works.** This page is the reference for what it is today. It is guarded by
`scripts/test-camera-rule.sh` (ctest `HoverNetCameraRule`: the numbers below against the source, and nothing in the graphics code touching the camera),
by the 28 render golden frames (`docs/render-golden.md`: they hash the picture the camera makes), and by `HoverNetRenderGolden <track> <scene> modern`
(its `cameradiff` must be 0: the look does not move the camera) and `... camerarate` (the same race drawn at two frame rates).

## What the camera is (`MR_Observer::Render3DView`, `NetTarget/Game2/Observer.cpp`)

| Part | Reference behaviour |
| --- | --- |
| Chase position | 3.4 m behind the craft's heading and 1.7 m above its base, both times the chase scale (`SetChaseScale`, the C key steps it; 100% by default) |
| Chase angle | the craft's own heading, not smoothed |
| Chase smoothing | once per drawn picture the camera is (3 x target + last position)/4 sideways and (2 x target + last)/3 up and down (`SmoothChaseCamera`): the original rule, restored 2026-11-09 at the owner's request |
| Cockpit position and angle | 256 mm behind the craft's centre, 1.05 m above its base, looking where the cabin points |
| Field of view | the observer's aperture (`MR_PI/2`), unchanged by speed, turning, effects or anything else |
| Vertical placement | `Scroll()` in the client's set-up (a wide window nudges the horizon so the craft is not cut off) |
| Countdown | before the start the camera swings round and back during the first seconds (`pTime < -3000`) |
| Split screen, debug views | unchanged |

## What the camera is given (changed 10 November 2026, at the owner's request)

Offline play now runs the simulation in the same whole 16 ms ticks as the server and the replays (so a race plays the same on every machine) and draws each picture between the last two ticks:
every free element, the viewed craft included, is placed part of the way from where it was before the last tick to where it is after it (`MR_GameSession::BeginInterpolatedRender`, called once
from the client around `RenderNormalDisplay`), and the race time handed to the observer is the matching moment. **The camera's arithmetic and every number above are unchanged**; what changed is
its input: the craft position and the race time it is handed are the interpolated ones, so it follows a craft that moves smoothly at any frame rate. Replays and online races are not
interpolated. `scripts/test-camera-rule.sh` pins that only the client's render call asks for it. `HOVERNET_NO_INTERPOLATION=1` turns it off and `--variable-step` brings back the old
clock-driven stepping (both for comparison and tests).

## What this rule forbids

Any change to the above by graphics or other work, including: widening or narrowing the field of view, zooming, shaking, looking ahead of the craft,
extra smoothing or less, a different chase distance or height, a lens or camera effect that moves the picture, a new call that moves the camera.
The Modern look may only change what is drawn *in* the picture the camera makes, never the camera.

## History

2.3.28 widened the field of view with speed (up to 8%), closed the vignette in with speed and adapted the exposure to the view; the owner found the craft
seemed to move away from the camera, especially in turns, and that the camera work had been good before. They were removed (2.3.29 work) and this rule was
made permanent. The chase smoothing was also made independent of the frame rate then (it had been done once per drawn frame, so a slower frame rate made
the camera trail the craft more): that is the reference behaviour above, equal to the old one at one picture per tick.

2026-11-09: the owner asked, in so many words, for the camera to follow exactly as before 2026-11-08. The race-time smoothing was replaced by the original per-picture rule above.
