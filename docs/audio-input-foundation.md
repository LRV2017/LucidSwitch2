# Audio and Controller Device Foundation

## Controller hub

The host-neutral controller hub accepts up to eight controller snapshots. Each snapshot contains a 16-bit logical button mask, four signed 16-bit stick axes, two normalized 0..32767 trigger values, a connection state, and a monotonically increasing per-slot sequence number. Updates and reads are synchronized for host input threads.

A future SDL or native platform adapter can translate physical devices into this common representation. Device discovery, hot-plug polling, Joy-Con pairing/rail protocols, HD rumble, NFC, motion sensors, touch input, and target-specific controller services are not implemented by this layer.

## PCM mixer

The audio mixer accepts signed 16-bit interleaved stereo PCM at a 48 kHz logical rate across 16 independent channels. It provides bounded per-channel queues, deterministic Q15 gain, saturated summation, a pull-style mix operation, and frame counters. An OS audio callback can pull buffers through `mix_stereo`.

The current layer does not resample, spatialize, decode proprietary game audio formats, reproduce console DSP/effects, synchronize audio clocks to a guest scheduler, or open a host audio device. It is a portable mixing/queue foundation, not complete console audio emulation.
