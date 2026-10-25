# EchoLabNT

A stereo delay plug-in for Expert Sleepers disting NT, with 20 treatments and 17 musical presets covering analog, tube, tape, platter, digital, multi-head, swept filters, reverse, ducking, auto-volume and looping. Shape repeats with tone, drive, modulation, CV and MIDI clock, or use the internal tempo.

Target: disting NT firmware 1.18.0 / API 13.

## Install

1. Download and extract the release archive.
2. Back up your current presets and plug-in files.
3. Copy `echolabnt.o` to `/programs/plug-ins/` on the module's MicroSD card.
4. Safely eject the card, insert it into the module and restart.
5. Add **EchoLabNT**, then choose a model and set Time, Feedback and Mix.

See [installation and controls](docs/INSTALL_NT.md) for routing, clock and modulation settings.

Internal tempo starts at 120 BPM. Choose a musical recipe from **Model → Preset**; note divisions follow the selected clock. See the [preset guide](docs/PRESETS.md).

**Stereo Weave** plays a 3:2 rhythm across the two outputs. For a mono source, select the same input bus for both Input L and Input R; use separate left and right output buses to hear the stereo pattern.
