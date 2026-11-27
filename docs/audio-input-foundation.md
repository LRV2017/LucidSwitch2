# Audio and Controller Device Foundation

## Controller hub

The host-neutral controller hub accepts up to eight controller snapshots. Each snapshot contains a 16-bit logical button mask, four signed 16-bit stick axes, two normalized 0..32767 trigger values, a connection state, and a monotonically increasing per-slot sequence number. Updates and reads are synchronized for host input threads.

## Optional SDL2 host adapter

SdlHostIo uses SDL2 GameController polling to discover supported desktop gamepads, track connect/disconnect, map standard face/shoulder/menu/stick/D-pad buttons, and publish normalized stick/trigger snapshots. It polls at the caller's request; the application controls its input-poll cadence. Up to eight controllers are tracked. SDL rumble, Nintendo Joy-Con rail pairing, motion sensors, NFC, touch input, and controller battery reporting are not implemented.

The same adapter can open a 48 kHz, signed-16-bit, stereo SDL audio device. Its callback pulls mixed frames from AudioMixer. If SDL2 was not found at configure time, the adapter compiles as a stub and reports that host-device support is unavailable.

## PCM mixer

The audio mixer accepts signed 16-bit interleaved stereo PCM at a 48 kHz logical rate across 16 independent channels. It provides bounded per-channel queues, deterministic Q15 gain, saturated summation, a pull-style mix operation, and frame counters.

The SDL callback currently takes the mixer's mutex and consumes bounded std::deque queues. It is suitable as an initial device integration, not a hard real-time audio engine. A later implementation should use preallocated lock-free or single-producer/single-consumer ring buffers, add sample-rate conversion/resampling, clock drift correction, spatial mixing, and guest DSP/effect models.

## Guest compatibility boundary

SDL2 only connects Lucid's host mixer and normalized host gamepad input to desktop devices. It does not implement Horizon audio/input services, guest audio IPC, controller service state machines, console DSP, rumble commands, NFC or motion APIs. No console-specific service is claimed compatible.
