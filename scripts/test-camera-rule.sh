#!/usr/bin/env bash
# The camera is off limits (CLAUDE.md rule 7, docs/camera.md). Graphics, rendering and performance work must not change how the race camera works: where it
# sits, how it follows the craft, its field of view, the cockpit view. This is the source-level half of the guard (the other half is the golden frames,
# which hash the picture the camera makes, and HoverNetRenderGolden's `camerarate` and `cameradiff` checks, which prove the look and the frame rate do not
# move it). It checks that the reference values in docs/camera.md are still the ones in the source, and that nothing in the graphics code touches the camera.
# A deliberate camera change (only ever at the owner's explicit request) updates this script, docs/camera.md and the golden frames in the same change.
# Run from the repository root.
set -uo pipefail
cd "$(dirname "$0")/.."
obs=NetTarget/Game2/Observer.cpp
fail=0
need() {   # need <expected count> <fixed text> <what it pins>
  local n; n="$(grep -c -F -- "$2" "$obs")"
  if [ "$n" != "$1" ]; then echo "FAIL $3: expected $1 of '$2' in $obs, found $n"; fail=1; else echo "ok   $3"; fi
}
need 2 "int lDist = 3400*mChaseScale/100;"                                  "chase distance: 3.4 m (both camera set-ups)"
need 2 "lCameraPos.mZ  = pViewingCharacter->mPosition.mZ + 1700*mChaseScale/100;" "chase height: 1.7 m"
need 2 "lCameraPos.mZ  = pViewingCharacter->mPosition.mZ + 1050;"           "cockpit height: 1.05 m"
need 2 "lCameraPos.mX  = pViewingCharacter->mPosition.mX - 256*MR_Cos[ lOrientation ]/MR_TRIGO_FRACT;" "cockpit position: 256 mm behind the craft's centre"
need 2 "lOrientation   = pViewingCharacter->GetCabinOrientation();"         "cockpit looks where the cabin points"
need 2 "lOrientation   = pViewingCharacter->mOrientation;"                  "chase looks where the craft points (no smoothing of the angle)"
need 2 "mApperture = MR_PI/2;"                                              "the field of view: the observer's own aperture"
need 1 "m3DView.Setup( pDest, lXMargin, lYOffset+lYMargin, lXRes-2*lXMargin, lYRes-2*lYMargin, mApperture );" "the 3D view is set up with that aperture and nothing added to it"
need 1 "pTarget.mX = (3*pTarget.mX + mLastCameraPos.mX )/4;"          "chase smoothing: once per drawn picture, three parts target to one part last (sideways)"
need 1 "pTarget.mZ = (2*pTarget.mZ + mLastCameraPos.mZ )/3;"          "chase smoothing: two parts target to one part last (up and down)"
need 1 "pTarget.mY = (3*pTarget.mY + mLastCameraPos.mY )/4;"          "chase smoothing: sideways, y"
need 2 "int lFactor = (-pTime-3000)*2/3;"                                   "the countdown's opening swing of the camera"
# Nothing in the graphics code may touch the camera or the field of view.
for f in NetTarget/VideoServices/ModernGraphics.cpp NetTarget/VideoServices/ModernGraphics.h NetTarget/VideoServices/TextureEnhance.cpp NetTarget/GraphicsSDL2/SDL2Graphics.cpp; do
  if grep -n -i -E "SetupCameraPosition|aperture|apperture|field.of.view|fov\b|SetChaseScale|SetCockpitView|Scroll\(" "$f" | grep -v -E "^\s*[0-9]+:\s*//" | grep -v -i "never\|must not\|not change\|never touches\|do not\|untouched" >/dev/null; then
    echo "FAIL $f mentions the camera or field of view in code (see CLAUDE.md rule 7):"; grep -n -i -E "SetupCameraPosition|aperture|apperture|field.of.view|fov\b|SetChaseScale|SetCockpitView|Scroll\(" "$f" | head -3; fail=1
  else echo "ok   $f does not touch the camera"; fi
done
# The observer's camera is only changed through the calls the client already makes.
c="NetTarget/LinuxClient/Track3DViewer.cpp"
for pair in "observer->Scroll(:3" "SetChaseScale(:1" "SetCockpitView(:3" "SetEyeOffset(:2"; do
  token="${pair%:*}"; want="${pair##*:}"; n="$(grep -c -F -- "$token" "$c")"
  if [ "$n" != "$want" ]; then echo "FAIL $c: '$token' is used $n times, it was $want: a new way of moving the camera needs the owner's request"; fail=1; else echo "ok   $c: '$token' used as before"; fi
done
# The observer must not grow a new camera-moving member (a boost, a shake, a zoom, a look-ahead).
if grep -n -i -E "mApertureBoost|aperturebias|camerashake|shake|lookahead|look_ahead|zoombias|SetFov|SetAperture" "$obs" NetTarget/Game2/Observer.h | grep -v -E "^\S+:[0-9]+:\s*//" | head -3 | grep -q .; then echo "FAIL the observer has something that looks like a camera effect (a boost, shake, zoom or look-ahead)"; fail=1; else echo "ok   the observer has no camera effect"; fi
if [ "$fail" = 0 ]; then echo "camera rule: the camera is as it is documented in docs/camera.md"; exit 0; fi
echo "camera rule: FAILED -- the camera view must not be changed by graphics work (CLAUDE.md rule 7). If the owner asked for a camera change, update this script, docs/camera.md and the golden frames in the same change."; exit 1
