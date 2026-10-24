# Coloured_Echos installation

For Expert Sleepers disting NT, targeting firmware 1.18.0 / API 13.

## Install the plug-in

1. Extract the release archive.
2. Back up your presets and the existing plug-in file, if present.
3. With the module powered off, copy `coloured_echos.o` to `/programs/plug-ins/` on its MicroSD card. Keep only one copy of this plug-in in that folder.
4. Safely eject the card, return it to the module and power on.
5. Add **Coloured_Echos** from the algorithm menu.

The archive's `SHA256SUMS.txt` lists file checksums. Its `.profile` files are settings for the computer renderer, rather than presets that can be loaded directly on the module.

## Route audio

The default inputs are buses **1/2**, and the default outputs are **13/14**. Choose the input and output buses on the Routing page.

- **Replace** writes the processed signal to the output bus. Use this for an insert.
- **Add** mixes the processed signal with audio already on the output bus.
- **None** supplies silence at an input or disables an output.
- Sending both outputs to the same bus produces a mono average, using the left output mode.

Select **Max delay** when adding the algorithm: 1 to 8 seconds, with a default of 3 seconds. Time stays within that capacity.

## Set the sound

Choose a Model, then adjust **Time**, **Feedback** and **Mix**. The supplied profiles use **28% Feedback** and **39% Mix**. Model-specific pages expose controls such as Tone, Drive, Mod rate/depth, tap pattern, sample rate, ducking or swell.

**Sweep Down** uses a descending low-pass sweep; **Sweep Up** uses an ascending high-pass sweep. A new input phrase triggers each sweep, whose duration follows Mod rate.

**Internal modulation** switches the internal LFO, wow, flutter and drift together. External CV and the MIDI modulation wheel remain available. Two mapping slots connect a modulation source to Time, Feedback, Tone, Drive, Mix, Depth, Exciter or the 8bit sample rate. Choose CV buses on the CV inputs page; a ±5 V signal spans the modulation range.

The first **Delay** page shows **Time** in milliseconds in Free mode. With Internal BPM or a locked MIDI/CV clock, its first control becomes **Division**, using straight, dotted or triplet note values. The Clock page selects the clock source and internal tempo; CV also needs a Clock bus and pulses per quarter note. External clock waiting, Stop or more than two seconds without pulses returns the first control to manual Time.

On **Color**, **Version A** keeps the model's original character; **Version B** selects the alternate character. Models with a single character omit this selector. Existing saved settings are retained until you change Version.

For **Multi-Head**, open **Heads** and set **Tap pattern**. A pattern places taps within the complete delay span selected on the Delay page. In sync, Heads also offers the span's Division and displays the tap positions as notes where possible. The taps are preset patterns, rather than individually editable notes. D means dotted and T means triplet; the Golden pattern uses fractions of the span.

For **Stereo Delays**, open **Stereo**. In sync, **Division** sets the left delay and **Right division** selects the right delay as a musical note. Choose **Ratio** to use the stored R/L delay ratio instead. Free mode and a missing external clock use that ratio. Each channel is limited independently by Max delay. The screen reports max delay if a requested note exceeds this capacity.

## Bypass and looping

**Active** processes normally. **Dry** passes the input while existing echoes decay internally. **Tails** passes the input together with the remaining echoes. The output limiter remains active.

Loop Sampler provides **Record**, **Play**, **Overdub**, **Stop** and **Clear**. Time sets the next recording length. Audio stays in memory only; saved presets do not contain the loop, and changing the model clears it.
