# ResonaPro — working instructions

Standing preferences for work in this repository. These are the user's, not mine;
follow them without being asked again.

## Delivery: do not package unless asked

**Do not build `.pkg`, `.dmg`, or installer `.zip` files unless the user asks for
them.** They cost real time, they are only needed when a build is going to someone
else, and the user tests locally in FL Studio.

Unless packaging is requested, the end of a change is:

1. Build the plugin (`./.work/scripts/build.sh build`)
2. Run both test suites (`ResonaProDspTests`, `ResonaProTests`)
3. Install into the local plug-in folders (`./install.sh`)
4. Report what changed, with the measurements behind it

The packaging scripts (`.work/scripts/package_pkg.sh`, `.work/scripts/package_dist.sh`)
stay in the tree for when they are wanted. Do not run them by default.

## Keep the install current

The user's DAW scan reads `~/Library/Audio/Plug-Ins/VST3/ResonaPro.vst3` and
`~/Library/Audio/Plug-Ins/Components/ResonaPro.component`. After any source change
that affects audio, install, or the user will test a stale build and report a bug
that is already fixed. The version label on the interface shows what is actually
loaded — check it when a report does not match the source.

## Evidence over assertion

Every claim about audio behaviour in this project must be measured. The test suites
are the contract; a change that is not covered by a check is not verified. When a
change fails, report the measurement that failed it, including when the change was
mine and the numbers are unflattering.

Two failures in this repository's history were found only by measuring and would
have shipped otherwise:

- A gain-composition bug where overlapping band weights turned an 18 dB cut into
  roughly 10 dB
- A `log()` applied to a value already in dB, which produced NaN and silently broke
  the sibilance tracking

## File layout

- `Source/` — the plug-in. `Source/DSP/` is the engine, `Source/UI/` the interface.
- `Tests/` — `DspTests.cpp` and `ProcessorTests.cpp`, both built through CMake so
  they always compile against the current headers.
- `Tools/Analyze.cpp` — the file-based analysis harness.
- `docs/` — design notes, roadmaps and reports. Keep them current; several record
  what was tried and why it failed, which is worth more than what succeeded.
- `.work/` — build trees, probes, logs, backups, previews. Not deliverables.
- `outputs/` — only populated when the user asks for packages.

## Reporting

State what was measured, and separate what is verified from what is reasoned. The
distinction matters here: synthetic test signals prove the signal path is clean and
say nothing about whether the plug-in sounds right on a real voice. That still needs
the user's ears, and it is fair to say so.

## Priority order: quality first

The user's standing priority, stated directly: **audible quality outranks
everything else.** Latency, plug-in size and CPU cost are all acceptable
trade-offs in service of it, within reason.

Practical consequences:

- Do not shelve an approach because it costs latency, size or cycles. Shelve it
  only because measurement says it does not improve the sound, or because it is
  not achievable.
- When a quality improvement and a latency or CPU saving conflict, take the
  quality. Say what the cost was, but do not let the cost decide.
- "Within reason" still applies: a change that makes the plug-in unusable in a
  session, or that a typical machine cannot run, is not a quality improvement.
  State the measured cost so the user can judge it.
- This reopens work that was set aside for cost reasons. The multi-resolution and
  phase-coherent resynthesis refactor (Phase 3 of the quality roadmap) was shelved
  because it adds ~85 ms of latency and CPU. That reasoning no longer holds on its
  own; it now needs a quality argument either way, not a latency one.

Measured costs are still required. Quality-first changes the decision, not the
evidence standard: report what it cost as well as what it bought.
