# Beads porting notes

The imported Beads source is from Mutable Instruments' Eurorack repository at
`f4cfa7697095a28d292bff758beb51deb49b5a86`. The small compatibility helpers
in `dsp/stmlib_compat.h` are copied from stmlib revision
`e3bd7c9cc00e4364166f9905c0509b6ffd0535ec` (`dsp/dsp.h`'s `SmoothStep` and
`dsp/hysteresis_quantizer.h`'s `HysteresisQuantizer2`). Their code and MIT
license notice are retained; the declarations live in namespace `beads` so
Beads does not add APIs to its stmlib dependency.

The compatibility change is confined to these Beads files: added
`dsp/stmlib_compat.h`; helper includes and local API references in
`cv_reader.h`, `dsp/delay_grain.h`, `dsp/delay_line.h`,
`dsp/delay_controller.h`, `dsp/grain_envelope.h`,
`dsp/processor.cc`, `dsp/randomized_parameter.h`, `dsp/synth.h`, and
`dsp/trigger_generator.h`; and this `PORTING.md` note.

The stmlib submodule remains pinned at
`c0c42ec91d315a2ec9c0a3845e593b71ffa429db`. A full update was avoided because
that upstream revision changes `stmlib::Biquad::set_f_q`: exact mode keeps its
`0.497f` input cap while approximate modes gain a lower `0.158f` cap. That
would change filter response for existing Plaits, Rings, Clouds, and other
users of the shared dependency. Beads only needs the two additive helpers
above; the Beads call sites include the local compatibility header directly.

The original upstream README says the Beads code is provided as-is and that it
builds on desktop, but has not been verified to build and run on the module.
The same README identifies AVR code as GPL-3.0, STM32F code as MIT, and hardware
as CC BY-SA 3.0. See the pinned upstream
[README](https://github.com/pichenettes/eurorack/blob/f4cfa7697095a28d292bff758beb51deb49b5a86/README.md)
and [Beads source tree](https://github.com/pichenettes/eurorack/tree/f4cfa7697095a28d292bff758beb51deb49b5a86/beads).
