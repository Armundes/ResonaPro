# ResonaPro: interface suggestions

A review of the 960 x 700 interface, with changes ranked by how much they help.

The short version: the plugin has about 24 controls and no hierarchy. Every
control carries the same visual weight, so the eye has nowhere to land. The graph
— the one thing that makes this plugin different from a dynamic EQ — is the
smallest element on screen. Most of the work below is not decoration. It is
deciding what matters and letting the rest recede.

Nothing here changes how the plugin sounds.

---

## 1. The one change that matters most: make the graph the hero

The graph shows the spectrum, the reduction curve, the focus bands and the
detection. It is the reason to use this plugin. Right now it shares space with
two rows of knobs and a strip of toggles.

**Do this.** Give the graph the top two thirds of the window and make it fully
interactive:

* Drag a focus node to move it, scroll on it to change Q.
* Right-click a node for a small menu: solo, invert, reset, delete.
* Draw the reduction curve as a filled area under the spectrum, so depth reads
  as area rather than as a line.
* Show the detected resonances as short vertical marks at the frequencies the
  engine chose. This is the feature users of this kind of plugin ask for first,
  because it turns a black box into something you can disagree with.

**Why.** A user cannot tell whether the plugin is working unless they can see
what it decided. Right now they can see a curve, but not the decision behind it.

---

## 2. Split the controls into "always" and "if you want"

Right now all 24 controls sit on one surface. Split them by how often a mixing
engineer touches them.

| Tier | Controls | Where |
|---|---|---|
| Always | Depth, Selectivity, Match, Mix, the graph, Preset, A/B | On the surface |
| Often | Quality, Response, Attack, Release, Max Cut, Vocal Profile, focus bands | On the surface, grouped |
| Occasionally | Stereo Link, Mid/Side, Hard, Ear Guard, Transient, Sibilance, the three tilts, Note Motion, Low Detail, Ext Key | Behind a disclosure |

The editor already has a drawer. **Use it for the third tier and close it by
default.** That alone removes about ten controls from the first impression
without removing any capability.

---

## 3. Kill the toggles that are really modes

Five on/off buttons sit in the same row and read as equals, but they do very
different things.

| Control | Problem | Suggestion |
|---|---|---|
| `MATCH` | An action everyone wants on. Few will ever turn it off. | Default it on. Make it a small checkbox in the output area, not a button in the main row. |
| `EAR GUARD` | A weighting choice. It is not a guard. | Rename to **Loudness weighting**, and put it in the drawer with the other tone options. |
| `HARD` | A behaviour choice with a real cost. | Keep visible, but show the state in the label, for example `HARD` lit when on. |
| `MID/SIDE` | Changes what the graph means. | Keep visible. Add a graph tint when it is on, so the user cannot forget. |
| `EXT KEY` | Only meaningful with a sidechain routed. | Grey it out until a sidechain is present. A control that cannot act should look like it. |

**Why.** Five equal-looking buttons make the user read all five every time. A
mode that is on 95 percent of the time should not compete for attention with one
that is a genuine choice.

---

## 4. Say what each control does, in plain words

Current labels assume the user already knows the plugin. Rename the ones that
carry no meaning on their own.

| Now | Suggested | Why |
|---|---|---|
| `SELECTIVITY` | **How narrow** | Says what changes, not what it is like |
| `SHARPNESS` / `DETAIL` | **Detail** (pick one, use it everywhere) | Two names for one control appears in the UI today |
| `ATK TILT` / `REL TILT` / `DETAIL TILT` | **Attack by frequency**, **Release by frequency**, **Detail by frequency** | "Tilt" is jargon; the tooltip then has nothing left to add |
| `TRANSIT` | **Transient guard** | Truncated label reads as a transport control |
| `RESPONSE` | **Overlap** | This is what the setting is |
| `QUALITY` | **Resolution**, with the Hz shown | The values are FFT sizes; "quality" hides that |
| `LOW DETAIL` | **Low band detail** | Three words, no cost |
| `EAR GUARD` | **Loudness weighting** | It is a weighting curve |

Also: the **headings** currently mix two ideas. `VOCAL TONE & CLARITY`,
`SIBILANCE & AIR` and `LEAD VOCALS` are three headings for what is one idea
(the vocal profile). Use one heading, or drop the headings and let the grouping
carry it.

---

## 5. Give every control a readable value

A row of identical arc knobs forces the user to hover each one. Add a small
persistent readout under each knob showing the value and, where it exists, the
unit — `2.0`, `-12 dB`, `23 Hz`, `2x`. The editor already computes an info
string, so the plumbing exists.

**Why.** Numbers let a user set a value on purpose and return to it later.
Knob position alone does not.

---

## 6. Quality-of-life changes, in order of payoff

These cost little and remove friction the user hits on every session.

| Change | Why |
|---|---|
| **Right-click any knob to type a value** | Precise recall. Standard in every plugin of this type. |
| **Double-click resets to default** (already fixed) and shows the default in the tooltip | Discoverable, and the tooltip already exists |
| **Copy and paste the whole band set, not one band** | The user tunes a voice, then wants it on the double. Paste already works across instances; extend it to the full set. |
| **Undo for band edits** | Dragging a node and losing the previous setting has no recovery today |
| **Drag on the graph to draw the focus shape directly** | Faster than moving nodes one at a time |
| **Remember the drawer state and window size** | Every reopen costs the same two clicks otherwise |
| **Keyboard: arrow keys nudge a selected node; Tab cycles nodes** | Precision without the mouse |
| **A "flat" button next to Reset Bands** | The current reset asks for a decision in the middle of one |

---

## 7. Visual language

The cream palette works and reads well against a spectrum. Keep the direction
and tighten the details.

* **One accent colour, one meaning.** Today the accent marks depth, focus, and
  reduction. Give reduction its own hue so the two curves never read as one.
* **Weight carries meaning.** Primary values at full contrast; secondary labels
  at 60 percent. Right now everything sits at the same weight, so nothing leads.
* **Group with space, not with boxes.** The section boxes add eight lines to the
  screen and no information. Space does the same job and reads calmer.
* **The graph needs a baseline.** A faint horizontal line at 0 dB and a dB grid
  at 6 dB steps lets a user read the curve instead of estimating it.
* **Frequency labels every octave, not fewer.** The graph spans 20 Hz to 20 kHz;
  at present the labels are thin at the low end, where vocals actually live.
* **Empty state.** When the plugin is doing nothing, say so in the graph area —
  "no resonances found". That single line answers the most common support
  question for this kind of plugin.

---

## 8. Two things to remove

* **`RESET BANDS`** beside `COPY`, `PASTE` and the A/B pair makes a row of four
  unrelated buttons. Move reset into the graph's right-click menu, where it
  belongs.
* **Duplicate naming.** `SHARPNESS` and `DETAIL` both appear. Pick one word and
  use it on the knob, the label, the tooltip and the docs.

---

## 9. Layout, concretely

```
+--------------------------------------------------------------+
|  RESONA   [preset v]              A/B    BYPASS    2.3.1      |  56 px
+--------------------------------------------------------------+
|                                                              |
|            spectrum + reduction + focus nodes                |
|                                                              |  ~400 px
|            detected resonances shown as marks                |
|                                                              |
+--------------------------------------------------------------+
|  DEPTH   HOW NARROW   ATTACK   RELEASE   MAX CUT   MIX   MATCH|  110 px
|   2.0       0.50       40 ms    180 ms    -18 dB   100%  [x] |
+--------------------------------------------------------------+
|  [v] Advanced                                       RESOLUTION |  40 px
+--------------------------------------------------------------+
|  Quality   Overlap   Profile   Low band detail   Sidechain     |  drawer
+--------------------------------------------------------------+
```

The first row answers "what is it doing". The second answers "how much". The
third is behind the drawer.

---

## 10. What to do first

If only three changes happen, do these.

1. **Make the graph the hero and show the detected resonances on it.** This is
   the difference between a plugin people trust and one they guess at.
2. **Move the third tier into the drawer, closed by default.** Removes about ten
   controls from the first impression.
3. **Add a value readout under every knob.** Ends the hover-to-learn loop.

---

## A note on how this document was made

These are design suggestions from reading the source and the rendered interface,
not from watching anyone use the plugin. The roadmap's own rule applies: a change
that cannot be shown to help should not be kept. Run each of these past two or
three engineers on a real session before building it, and drop whatever they do
not miss.
---

## What was built, 6 October 2026

Shipped in 2.3.1.

**Done**

* The drawer holds the occasional controls and is **closed by default**, so the
  first impression is the graph and the eight controls that matter. It carries
  Weighting, Low Band Detail, Ext Key, Reset Bands, Note Motion and the four
  tilt and de-ess knobs. Its state persists across editor reopen.
* Five equal-looking toggles on one row became three. Weighting and Reset moved
  into the drawer.
* The graph takes the full width unless the drawer is open.
* **Detected resonances are drawn on the graph.** Each reduced region gets a tick
  above its peak, so a user can see the decision and not just the curve.
* Controls renamed: `QUALITY` to `RESOLUTION`, `RESPONSE` to `OVERLAP`,
  `SELECT` to `HOW NARROW`, `EAR GUARD` to `WEIGHTING`, `LOW DETAIL` to
  `LOW BAND DETAIL`.
* **Units on every readout that has one**: `24.0 dB`, `8.0 ms`, `70.0 ms`,
  `0.0 dB`, `100.0 %`.
* The help line that collided with the knob labels is gone. The tooltips carry
  the same content.

**Deferred, with reasons**

* *Transient* and *Stereo Link* show the fraction (`0.50`, `1.00`) rather than a
  percentage. Both run 0..1, and JUCE ignores a text formatter set after the
  attachment. A wrong percentage would be worse than a plain number, so it waits
  for a proper fix.
* Right-click to type a value, undo for band edits, keyboard node nudging and
  drag-to-draw the focus shape are not built.
* The per-knob readout was built and then removed. It duplicated the text box the
  sliders already carry. Adding the units to the existing readout was the real
  fix.
