# Edo

A digital oscillator with a sub, cut to the shape of a Noise Engineering Incus
Iteritas Alia as far as four pots reach. Four waveforms run at once, locked to
the same pitch, and one pot slides across them:

```
sine ------ triangle ------ square ------ saw
```

## Pitch

P1 covers five octaves from C1, CV1 adds five more on top at one volt per
octave, so a full turn of the pot and a five volt patch reach the same
distance. The two sum, which makes P1 the coarse tune and CV1 the keyboard.
The oscillator is held between 8 Hz and 8 kHz however far the pair add up.

If the CV input turns out to span something other than five volts, that is one
constant at the top of the file, `kCvOctaves`, and `kBaseFreq` is the note
sitting at the bottom of P1.

## Shape

P2, with CV2 summed on top, crossfades sine to triangle to square to saw, the
same four the Incus's Shape covers.

The waveforms share a pitch but not a shape, so where two of them overlap they
partly cancel. A straight linear fade drops about 1.6 dB in the middle of each
leg; a full equal power fade fills that back in but peaks 20 percent hotter
than any single waveform, which the outputs cannot hold. The fade sits between
the two, holding the dip to a fifth of a dB.

Two things about the levels are deliberate. The square is the quieter of the
four by about 3 dB at the peak, which is how DaisySP levels a square against a
sine by loudness rather than by peak. And the triangle is left where it lands
relative to the sine: it comes out of an integrated square, so it already sits
a quarter cycle away, and "aligning" the sine to it cancels the middle of that
leg almost completely.

## Weight

P3 is Weight with Alloy all the way down, which on the Incus leaves it as a
plain saturation control. It sits behind the crossfade, so it works on whatever
the morph hands it rather than treating each waveform separately, and the clean
voice is crossfaded back in underneath so the pot at rest is a true bypass
rather than the saturator's own minimum.

It does not treat the four waveforms equally, and it cannot:

| Waveform | What Weight does |
|----------|------------------|
| Sine | the most work, from a pure fundamental to a full spectrum |
| Triangle | almost as much |
| Saw | fills out rather than brightens |
| Square | level only |

A square is already flat topped, so a soft clipper finds no peaks to round. An
asymmetric curve does not rescue it either. Everywhere between the four the
saturation has something to bite into, so this only really shows at the one
point on the pot.

Saturation puts its harmonics above the fundamental, and by the top of the
pitch range there is no room left for them below Nyquist, so Weight is pulled
back to half its reach as the pitch climbs past 1.5 kHz.

## Sub

P4 sets the level of a sub oscillator running below the main one, on a squared
curve so the bottom of the pot is usable rather than a step.

Output 1 carries the voice with the sub mixed under it. Output 2 carries the
sub on its own, at the same level, so the second jack is a bass send rather
than a copy. Both jacks stay under full scale at every combination of the four
pots, so nothing is clipping to hold the level down.

## Controls

| Control | Function |
|---------|----------|
| P1 | Pitch, five octaves from C1 |
| P2 | Shape, sine to triangle to square to saw |
| P3 | Weight, saturation |
| P4 | Sub level |
| CV1 | Pitch, 1V/oct |
| CV2 | Shape, summed onto P2 |
| B1 | Sub one or two octaves down |
| B2 | Sub sine or square |

## LED

- **Colour** says where the morph is sitting, sine blue, triangle green,
  square amber, saw red, blended the same way the audio is.
- **Brightness** says how hard Weight is working.
- **White** washes in as the sub comes up.
- **A button press** takes the LED for a moment to report what it changed: the
  sub's waveform as a colour, warm for sine and cyan for square, and its octave
  as a brightness, dim for one down and bright for two.
