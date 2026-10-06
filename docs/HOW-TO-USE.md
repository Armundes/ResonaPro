# How to Use ResonaPro

## 1. Getting it into your DAW

**ResonaPro 2.2.0 is the current build.** It sits in
`~/Library/Audio/Plug-Ins/VST3` and `~/Library/Audio/Plug-Ins/Components`, and
Apple's `auval` validator accepts the AU. You only need this section if you
rebuild the plugin later, or want it on another machine.

Here is where each build lands, so you can confirm a version without guessing.
Look at the small text under the title in the interface: it prints the version
of the binary that is actually running.

| Build | Installed at | How your DAW finds it |
|---|---|---|
| VST3 | `~/Library/Audio/Plug-Ins/VST3/ResonaPro.vst3` | FL Studio scans this folder by default. Use this one. |
| AU | `~/Library/Audio/Plug-Ins/Components/ResonaPro.component` | For Logic, GarageBand and other Audio Unit hosts. |
| Standalone | `/Applications/ResonaPro.app` | Launch it from Spotlight, Launchpad or Finder. No DAW needed. |

If the version text reads anything other than **v2.2.0**, the host is running a
cached copy and needs to rescan. The FL Studio steps are at the end of this
section.

**Option A — run the installer (recommended)**

```bash
cd /Users/armundescarey/Documents/ResonaPro
./install.sh
```

Run it from Terminal, not from inside an app.

**Option B — copy it yourself in Finder**

1. Press `Cmd+Shift+G` in Finder and go to `~/Library/Audio/Plug-Ins/VST3`.
2. Move any old `ResonaPro.vst3` to a backup folder outside `VST3`.
3. Drag `ResonaPro.vst3` from the release archive into that window.
4. Repeat for the AU in `~/Library/Audio/Plug-Ins/Components` with `ResonaPro.component`.
   The installer signs each copied bundle on this Mac and keeps a rollback copy;
   use it instead of Finder when possible.

**Option C — no DAW needed**

Open `ResonaPro.app` from your Applications folder, or from Spotlight. It runs as a standalone processor with its own audio input and output, so you can hear it with no host involved. `./install.sh` puts it there; a copy also stays in `Binaries/`, but an app left inside a project folder does not show up in Spotlight or Launchpad.

After either option, rescan plugins in your DAW. In Logic, quit and reopen the app. In Ableton, hold `Option` while clicking Rescan. In REAPER, click "Clear cache and rescan".

### FL Studio (macOS)

FL Studio caches plugins by file path, so replacing a plugin with the same file
name can go unnoticed. Force it to re-read:

1. Open FL Studio.
2. Go to **Options → Manage plugins**.
3. Tick **Rescan previously verified plugins**. This step matters. Without it FL
   skips the file because the path already exists in its database.
4. Click **Start scan**.
5. Open the Browser and look under **Plugin database → Installed → Effects**.
   ResonaPro appears there, and also under **VST3**.

To check it faster, find ResonaPro in the Plugin Manager list, select it, and
click **Verify plugin**. That forces a re-read of the one file.

If ResonaPro still shows the old behaviour, right-click it in the Plugin Manager
and remove it, then scan again. That clears the stale entry.

Use the **VST3** version in FL Studio. It is the better supported format there.

**On latency:** FL Studio compensates plugin delay on mixer inserts, so the
46 ms of the Standard quality setting costs you nothing on playback. It does
affect live monitoring through the plugin, so switch to **Low Latency (1k)**
if you are tracking a vocalist and hearing a delay in their headphones.

---

## 2. First five minutes

1. **Load it on a vocal track.** Insert ResonaPro as the first plugin in the chain, before compression and EQ.
2. **Turn on DELTA.** Press the DELTA button and loop a phrase. You now hear only what the plugin removes. This is the fastest way to understand it.
3. **Raise DEPTH.** Start at 0, then move up. You will hear the harshness, the boom, and the sibilant edges come forward as you turn it up.
4. **Turn DELTA off.** Now set DEPTH to where the vocal sounds cleaner but still sounds like the singer. Depth 1 to 1.5 covers most work.
5. **Check the MIX.** If the vocal lost body, pull MIX back to 70 to 80 percent instead of reducing Depth. Parallel blending keeps the tone.

---

## 3. The controls in the order you should reach for them

| Order | Control | What to do |
|---|---|---|
| 1 | **VOCAL PROFILE** | Pick your goal: Lead Vocal, De-Ess, Warm Body, or Air. This aims the detector before you touch anything else. |
| 2 | **DEPTH** | The main amount. 0 is transparent. 1 is gentle. 3 is heavy. |
| 3 | **DELTA** | Use this constantly. It tells you whether the plugin is working on the right thing. |
| 4 | **SELECT** (Selectivity) | Raise it if the plugin touches things it should leave alone. Lower it if it misses obvious harshness. |
| 5 | **DETAIL** (Sharpness) | Low values act like a dynamic EQ. High values act like a surgical notch. |
| 6 | **TRANSIENT** | Raise it when consonants and plosives get dull. Lower it for de-essing. |
| 7 | **MAX CUT** | The hard ceiling on any single frequency. Lower it if the result sounds hollow. |
| 8 | **FOCUS BANDS** | Fine-tuning. Drag a node up to push the detector harder in that range, down to protect that range. |
| 9 | **ATTACK / RELEASE** | Leave at 8 ms and 70 ms unless you need faster sibilance control or slower low-end control. |
| 10 | **STEREO LINK** | Keep at 1.0 for a stable image. Set to 0 when the two sides of the take differ enough to need separate treatment. |

**MID/SIDE** splits the centre from the sides. Use it when a reverb or doubled layer carries the harshness, not the main voice.

**EAR GUARD** applies a small, fixed ear-sensitive threshold bias. It is not
an ISO 226 calculation. The saved parameter ID remains `iso226` so older projects
still load.

**HARD** makes the detector react to loud peaks instead of relative peaks. It is much stronger. Treat it as a separate mode, not a volume knob.

---

## 4. Starting points that work

**Straight lead vocal**
Profile Lead Vocal, Depth 1.0, Select 0.5, Transient 0.5, Mix 100.

**Harsh 3 kHz range, the "sting"**
Preset "Lead Vocal - Tame Harshness". Then drag the focus node near 3 kHz up until Delta plays back the sting on its own.

**Sibilance, the "sss"**
Preset "De-Ess - Broad / Lispy" for a soft, lisping singer. Preset "De-Ess - Whistling / Narrow" for a sharp, narrow whistle. Always check DELTA to confirm you hear only the "sss".

**Plosives and clicks getting dull**
Preset "Plosive Safe De-Ess". It protects attacks, so "P", "T" and "K" stay crisp.

**Muddy low mids**
Preset "Warm Body - De-Mud".

**Dull top end**
Preset "Air & Silk Polish".

**Whole mix bus**
Preset "Harsh Bus Glue". It uses a 4k transform and a 12 dB ceiling, which suits bus work.

---

## 5. Settings that save CPU

| QUALITY | Latency at 44.1 kHz | Use it for |
|---|---|---|
| Low Latency (1k) | about 23 ms | tracking and monitoring |
| Standard (2k) | about 46 ms | mixing, the default |
| High (4k) | about 93 ms | bus and mastering |
| Ultra (8k) | about 186 ms | surgical work on a problem resonance |

**RESPONSE** sets how often the plugin updates its gain curve. Eco uses the least CPU, Fine uses the most.

Changing QUALITY or RESPONSE while audio plays causes a short gap and a latency change. Set these with playback stopped.

Next to those, a readout tells you what the current setting actually means — the
transform length, the number of analysis bands, the spacing between them in Hz,
and the latency in milliseconds. At 96 kHz a "2k" transform has half the
frequency precision it has at 48 kHz, and the readout is the honest answer.

---

## 6. The control strip along the bottom

| Control | What it does |
|---|---|
| **MATCH** | Level matching, and the most useful switch on the panel. Removing resonances costs level, about 1.3 dB on a typical voice at Depth 1.5, and a quieter signal nearly always sounds better, so an A/B comparison lies to you without this. MATCH trims the output to the input's **perceived loudness** using ITU-R BS.1770 K-weighting rather than raw RMS, because the ear does not weight frequencies equally and a resonance suppressor works at chosen frequencies. Leave it on and you judge tone instead of loudness. It never raises the peak above what the input already had, so it cannot add clipping, and it switches itself off in BYPASS and DELTA where matching would be meaningless. |
| **A/B** | Two stored settings, swapped by the button. The label shows which slot is live. Press it once to set the reference, dial in a change, press again to compare. |
| **DELTA BAND** | Restricts DELTA to one focus band. Set to "All" for the usual behaviour, or pick a band number to hear only what is being removed from that one region. This is what turns the graph from a guess into a tool — solo band 3, hear exactly what is happening at 3 kHz, adjust that node, listen again. |
| **COPY / PASTE** | Moves all eight focus band settings between instances. Set the bands up once on the lead vocal, copy, then paste into the doubles and the backing stacks. |
| **RESET BANDS** | Returns all eight focus bands to neutral. |

**Focus-node precision:** Hold **Shift** while dragging a node to snap its
frequency to a 1/6-octave grid and its sensitivity to whole dB steps. Right-click
a node to type `frequency gain Q`, such as `3200 3.0 1.4`, then press Return.
Mouse-wheel over a node changes Q; double-click toggles the node.

## 7. New analysis controls

| Control | Use it when | What changes |
|---|---|---|
| **LOW DETAIL** | A low-mid ring is hard to judge with a 1k or 2k transform. | Adds a 4k analysis window below 1.2 kHz, then blends its detection result into the main transform. It does **not** run a second audio synthesis path or add host latency. It may protect a harmonic or increase reduction, depending on the signal. At 48 kHz in a synthetic CPU test, 2k/4× rose from 1.5% to 4.7% of one core; 1k/4× rose from 1.4% to 7.9%. At 4k and 8k quality it has no extra effect. |
| **NOTE MOTION** | Low harmonics move during a phrase and the detector mistakes them for a fixed ring. | Compares the same frequency with the expected movement of a harmonic between frames. This is a **bounded hint**, not automatic pitch correction. Start near 0.5, then check DELTA. It needs a clear note change; on steady notes it does little. |
| **EXT KEY** | Another track should steer the detector while the vocal remains the processed output. | Reads the optional mono or stereo sidechain bus. If the bus is disabled, detection falls back to the vocal. If a bus is enabled but silent, the key may produce little reduction. |

### Route EXT KEY in FL Studio

1. Put **ResonaPro VST3** on the vocal's Mixer track.
2. On the track you want to use as the key, right-click the send switch to the
   vocal track. Choose **Sidechain only to this track**. The key does not join
   the audible vocal path.
3. Open ResonaPro's **wrapper settings → Processing → Inputs**. Assign that
   sidechain to the plugin's extra input. FL Studio's **Auto map inputs** can
   assign it after you create the sidechain send; check the mapping if you use
   several sends.
4. Turn on **EXT KEY** in ResonaPro. The vocal still supplies the audio output.
   Use DELTA to hear whether the key steers the bands you want.

These FL routing steps follow the [Image-Line Mixer manual](https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/mixer.htm)
and [Plugin Wrapper manual](https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/plugins/wrapper.htm).

### Learn a take

1. Turn **EXT KEY** off. Leave the plugin editor open.
2. Press **LEARN**, play the vocal phrase or whole take, then press **STOP LEARN**.
   Capture runs at 20 snapshots per second for up to 10 minutes. It needs at
   least one second of active frames and stops counting when FL Studio stops
   sending audio.
3. Read the proposed frequencies. If they fit what you hear in DELTA, press
   **APPLY LEARN**. Until you press Apply, **no parameter changes**.
4. Apply fills up to eight focus nodes with modest, positive detector sensitivity.
   It does not print a static EQ curve, change the source audio, or analyse a
   file while FL Studio is closed. You may still drag and fine-tune each node.

For a quiet or breathy take, Learn may propose no peaks. This is safer than
forcing a curve from too little evidence.

### Tooltips

Hover any control and a description appears after about a second. Every knob,
button, and menu has one.

---

## 8. Things that catch people out

- **The plugin is active when you load it.** Depth defaults to 1.0. If you want a clean starting point, choose the "Init (Neutral)" preset.
- **Depth at 0 is a true bypass.** The signal passes through sample-aligned with no processing.
- **DELTA is not a solo of the resonances in isolation.** It plays the removed signal at its natural level and timing, so it blends with your track. That is what makes it useful.
- **It will not fix broad tonal balance.** If the whole vowel sits 3 dB too dark, that is an EQ job. This plugin works on narrow peaks.
- **Below about 150 Hz it holds back.** A low resonance is hard to tell apart from the note's own harmonics, so the plugin prefers to leave the fundamental alone. Use the Warm Body profile or a focus node when you need work down there.
- **Judge it in context.** A vocal that sounds great soloed can sit wrong in the mix, and the reverse happens too.
