# PR: Export live wheel-ground state for renderer skidmarks

## Summary

Headless replay simulation now exports wheel contact and ground positions from
the live simulation state. This restores usable skidmark inputs without
running the game's presentation-only feedback snapshot pass.

## Root cause

`ReplaySimulationRuntime::CurrentRaceCameraState()` previously read wheel
contact flags and world surface points from `currentPhysicsState` and
`asyncState`. Those structures are refreshed by the game renderer's feedback
path, but the canonical headless sandbox deliberately does not run that path.
Consequently, a valid replay could report thousands of contacts and sliding
wheels while every `wheelHasSurface` value was false and every exported ground
position was `(0, 0, 0)`.

## Fix

The runtime now:

- reads contact presence from `wheel.realTimeState.contactPresent`;
- uses that authoritative contact as `wheelHasSurface`;
- reconstructs the wheel-bottom point from
  `wheel.surfaceHandler.CurrentPoint() - rollingRadius`; and
- transforms that local point with
  `state_->body.CaptureCurrentFrame().Location()`, the live body frame used by
  collision response.

Using the live body frame is essential. `ReplayPhysicsState().corpusIso` is
another presentation snapshot and remains the zero/default transform in this
headless path.

## Regression coverage

The optional real-replay fixture test simulates a recorded replay with the
Optimized CPU backend and checks every sliding contact for:

- a matching surface flag;
- finite and nonzero ground coordinates; and
- a ground point within five metres of the live car position.

On `Abuk.Replay.Gbx`, the regression covers 1,739 contacted wheel samples and
440 sliding-contact samples. All 440 now have a surface and a finite, nonzero,
nearby ground point; the old implementation fails all 440.

The test skips with code 77 when external Packs/replay fixture arguments are
not supplied, matching the repository's other installed-asset integration
tests.
