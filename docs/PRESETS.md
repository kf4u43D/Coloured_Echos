# EchoLabNT presets

Choose **Preset** on the Model page. The bank contains 19 musical starting points; **Custom** keeps your own settings. A new instance starts with Internal BPM at 120 BPM. Preset changes retain an active clock source and your tempo. Selecting a musical preset from Free enables Internal BPM.

The profiles store note values, not a fixed delay in milliseconds. D means dotted and T means triplet. At 120 BPM, a quarter note lasts 500 ms and a sixteenth lasts 125 ms. External MIDI/CV clock can drive the same divisions.

| Preset | Use | Division | Feedback | Mix | Tone |
| --- | --- | --- | ---: | ---: | ---: |
| Clean Quarter | Clear repeats for rhythmic accompaniment. | 1/4 | 28% | 39% | 16000 Hz |
| Wide Dotted | Moving dotted echoes for sustained parts. | 1/8D | 22% | 39% | 16000 Hz |
| Clocked Grain | Precise repeats with a restrained digital texture. | 1/8 | 22% | 39% | 16000 Hz |
| Analog Bounce | Dark, compact echoes around the dry phrase. | 1/8D | 13% | 39% | 2400 Hz |
| Tape Space | Warm quarter-note echoes with gentle motion. | 1/4 | 14% | 39% | 4100 Hz |
| Tube Slap | A short body-building slap behind the dry sound. | 1/16 | 10% | 30% | 3000 Hz |
| Ducked Voice | Echoes that leave room for the incoming phrase. | 1/4 | 28% | 39% | 4800 Hz |
| Bloom | A fully wet swell for soft entrances and pads. | 1/16 | 22% | 100% | 4400 Hz |
| Reverse Pad | A spacious reverse layer for sustained material. | 1/2 | 20% | 65% | 3800 Hz |
| Split Dots | Two complementary rhythms across the stereo field. | 1/4 / 1/8D R | 24% | 39% | 4600 Hz |
| Pingpong Width | Alternating repeats that open the stereo image. | 1/8 | 24% | 39% | 5200 Hz |
| Crushed 8bit | Grainy repeats that become coarser as the phrase fades. | 1/8D | 24% | 39% | 1800 Hz |
| High Spark | Bright, lightweight repeats above the main phrase. | 1/8 | 14% | 39% | 5000 Hz |
| Loop Bar | Capture and repeat one bar at the selected tempo. | 1/1 | 28% | 100% | 20000 Hz |
| 3 against 2 | A three-against-two rhythmic echo pattern. | 1/1 | 18% | 39% | 3500 Hz |
| 4 against 3 | A four-against-three rhythmic echo pattern. | 1/1 | 16% | 39% | 3900 Hz |
| 5 against 4 | A dense five-against-four rhythmic echo pattern. | 1/1 | 16% | 39% | 3300 Hz |
| Filter Lift | A rising high-pass echo gesture. | 1/4 | 20% | 50% | 5200 Hz |
| Filter Fall | A descending low-pass echo gesture. | 1/4 | 18% | 50% | 1900 Hz |

## Pick a role

**Clean Quarter** is the precise digital repeat. **Wide Dotted** adds slow stereo pitch motion. **Clocked Grain** reduces the internal sample rate, giving a distinct digital texture. The three engines serve different roles.

**Tube Slap** gives a short, restrained repeat. **Analog Bounce** and **Tape Space** use lower feedback to keep their driven repeats controlled. **Ducked Voice** reduces echoes while the input is present; **Bloom** uses a fully wet, 600 ms volume rise for soft entrances. Its short note delay keeps the swell in focus.

**Split Dots** sets different musical divisions on the two sides. **High Spark** filters the Exciter return below 300 Hz. **Loop Bar** records four quarter-note beats (one bar in 4/4) and stays in memory; its Tone is 20 kHz. Choose enough Max delay for long cycles: four beats at 60 BPM require four seconds.

**3 against 2**, **4 against 3** and **5 against 4** place two evenly spaced pulse grids inside one shared cycle. Coincident taps are accented. The Delay division sets the full cycle; each repeat restarts the same pattern.

## Production techniques

Short low-feedback repeats provide slapback depth; dotted echoes interlock with the played rhythm. These are established delay uses described in the [Neural DSP delay guide](https://neuraldsp.com/articles/how-to-use-a-delay-pedal).

Ducking leaves room for the original phrase, then lets echoes fill its gaps. Independent left/right timing and filtering are documented in the [Ableton Echo manual](https://www.ableton.com/en/live-manual/12/live-audio-effect-reference/#echo).

A polyrhythm combines different pulse counts across the same time span, with shared cycle boundaries; see [Splice's production explanation](https://splice.com/blog/polyrhythms-music-producer/).

Use the recipes as starting points. Feedback and drive interact, so final balance depends on source level, tempo and arrangement.
