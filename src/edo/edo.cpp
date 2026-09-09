#include <cmath>

#include "../../lib/carciofo.h"
#include "daisysp.h"

using namespace daisy;
using namespace daisysp;
using namespace carciofo;

/*
 * Edo. A digital oscillator with a sub, cut to the shape of a Noise
 * Engineering Incus Iteritas Alia as far as four pots reach.
 *
 * Four waveforms run at once, locked to the same pitch, and P2 slides across
 * them the way the Incus's Shape does:
 *
 *   sine ------ triangle ------ square ------ saw
 *   0             0.33           0.67          1
 *
 * P3 is Weight with Alloy all the way down, which on the Incus leaves it as a
 * plain saturation control. It sits behind the crossfade, so it works on
 * whatever the morph hands it rather than treating each waveform separately,
 * and it is crossfaded against the clean voice so the pot at rest is a true
 * bypass rather than the saturator's own minimum.
 *
 * Worth knowing before you reach for it: saturation does almost nothing to a
 * square. A square is already flat topped, so a soft clipper finds no peaks to
 * round and only changes the level, and that is not a shortcut taken here, it
 * is what the maths gives you. On the sine and the triangle it does the most
 * work, on the saw it fills out rather than brightens.
 *
 * P4 is the sub, a sine or a square one or two octaves down. Output 1 carries
 * the voice with the sub mixed under it, output 2 carries the sub on its own,
 * both at the level P4 sets, so the second jack is a bass send rather than a
 * copy.
 *
 *   P1 + CV1   pitch, 1V/oct
 *   P2 + CV2   shape, sine to triangle to square to saw
 *   P3         weight, saturation
 *   P4         sub level
 *   B1         sub one or two octaves down
 *   B2         sub sine or square
 */

/* ------------------------------------------------------------------ tuning
   These are the numbers worth reaching for. The pitch pair in particular is
   what the panel is calibrated against: kCvOctaves has to match the voltage
   the CV input actually spans, five octaves for a 0 to 5V front end, and
   kBaseFreq is the note sitting at the bottom of P1 with no CV patched. */

// Bottom of the pitch range, C1, and how far P1 and CV1 each reach above it.
static const float kBaseFreq = 32.7032f;
static const float kPotOctaves = 5.f;
static const float kCvOctaves = 5.f;

// Where the oscillator is allowed to run. The ceiling keeps the polyblep
// corrections meaningful.
static const float kMinFreq = 8.f;
static const float kMaxFreq = 8000.f;

/* Saturation range for Weight. The bottom is not zero because DaisySP's
   Overdrive mutes at a drive of zero, and the clean voice is crossfaded in
   underneath anyway, so the pot at rest is a true bypass either way. */
static const float kWeightMin = 0.15f;
static const float kWeightMax = 0.95f;

/* Weight is pulled back above this pitch, down to kWeightTameFloor at the top
   of the range. Saturation adds harmonics above the fundamental, and by the
   top octave there is no room left for them below Nyquist. */
static const float kWeightTameStart = 1500.f;
static const float kWeightTameEnd = 5000.f;
static const float kWeightTameFloor = 0.50f;

/* Curve of the crossfade windows. One is a linear fade, which dips about
   1.6 dB where two waveforms overlap; a half is equal power, which is flat but
   peaks 20 percent hotter than any single waveform. Three quarters holds the
   dip to a fifth of a dB for five percent of peak. */
static const float kMorphCurve = 0.75f;

// Output gains, set against the measured worst case over the whole panel so
// output 1 stays under full scale without a limiter holding it there.
static const float kMainGain = 0.57f;
static const float kSubGain = 0.36f;
static const float kSubOutGain = 0.90f;

// One pole coefficients per sample. Pitch is quick so playing stays snappy,
// the rest glide.
static const float kPitchSmoothing = 0.010f;
static const float kShapeSmoothing = 0.002f;

// How long a button press holds the LED to report what it just changed.
static const uint32_t kReadoutMs = 700;

// Identity colour per waveform, blended by the crossfade for the LED.
static const int kNumWaves = 4;
static const float kWaveRgb[kNumWaves][3] = {
    {0.10f, 0.30f, 1.00f},  // sine, blue
    {0.10f, 1.00f, 0.30f},  // triangle, green
    {1.00f, 0.55f, 0.00f},  // square, amber
    {1.00f, 0.10f, 0.20f},  // saw, red
};

static Carciofo hw;

static Oscillator osc[kNumWaves];
static Oscillator oscSub;
static Overdrive weight;

// Written by the control loop, read by the audio callback. Aligned 32 bit
// loads and stores are atomic on the M7, the same lockless handoff the other
// firmwares use.
static float freqTarget = 110.f;
static float subFreqTarget = 55.f;
static float mixTarget[kNumWaves] = {1.f, 0.f, 0.f, 0.f};
static float weightMixTarget;
static float subLevelTarget;

// Smoothed copies the callback actually runs on.
static float freq = 110.f;
static float subFreq = 55.f;
static float mix[kNumWaves] = {1.f, 0.f, 0.f, 0.f};
static float weightMix;
static float subLevel;

// What the buttons are holding, and what the LED is reporting about it.
static int subOctaves = 1;
static bool subIsSquare;
static uint32_t readoutTime;
static bool readoutIsWaveform;

static inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }

void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out,
                   size_t size) {
  for (size_t i = 0; i < size; i++) {
    freq += kPitchSmoothing * (freqTarget - freq);
    subFreq += kPitchSmoothing * (subFreqTarget - subFreq);
    for (int w = 0; w < kNumWaves; w++)
      mix[w] += kShapeSmoothing * (mixTarget[w] - mix[w]);
    weightMix += kShapeSmoothing * (weightMixTarget - weightMix);
    subLevel += kShapeSmoothing * (subLevelTarget - subLevel);

    // The morph. All four share a pitch, so they stay locked to each other.
    float voice = 0.f;
    for (int w = 0; w < kNumWaves; w++) {
      osc[w].SetFreq(freq);
      voice += osc[w].Process() * mix[w];
    }

    // Weight, crossfaded against the clean voice so it fades in from nothing.
    float main = Lerp(voice, weight.Process(voice), weightMix);

    oscSub.SetFreq(subFreq);
    float sub = oscSub.Process() * subLevel;

    // Output 1 is the voice, output 2 the sub on its own.
    out[0][i] = main * kMainGain + sub * kSubGain;
    out[1][i] = sub * kSubOutGain;
  }
}

// Reads the panel and hands the engine its targets.
static void ApplyControls() {
  float octaves = hw.GetPot(POT_1) * kPotOctaves + hw.GetCv(CV_1) * kCvOctaves;
  float f = Clamp(kBaseFreq * powf(2.f, octaves), kMinFreq, kMaxFreq);
  freqTarget = f;
  subFreqTarget = f / (subOctaves == 1 ? 2.f : 4.f);

  /* The crossfade as four triangular windows, one peaking on each waveform,
     bent by kMorphCurve before they are used: the waveforms share a pitch but
     not a shape, so they partly cancel where they overlap and a straight
     linear fade leaves an audible hole in the middle of each leg, while a full
     equal power fade fills the hole at the cost of a peak the outputs cannot
     hold. The exponent sits between the two. */
  float morph = 3.f * hw.GetPotWithCv(POT_2, CV_2);
  for (int w = 0; w < kNumWaves; w++) {
    float window = Clamp(1.f - fabsf(morph - static_cast<float>(w)), 0.f, 1.f);
    mixTarget[w] = powf(window, kMorphCurve);
  }

  // Weight, pulled back as the pitch climbs and its harmonics run out of room.
  float tame = 1.f;
  if (f > kWeightTameStart) {
    float t = (f - kWeightTameStart) / (kWeightTameEnd - kWeightTameStart);
    tame = Lerp(1.f, kWeightTameFloor, Clamp(t, 0.f, 1.f));
  }
  float amount = hw.GetPot(POT_3) * tame;
  weightMixTarget = amount;
  weight.SetDrive(Lerp(kWeightMin, kWeightMax, amount));

  // Squared so the bottom of the sub pot is usable rather than a step.
  float sub = hw.GetPot(POT_4);
  subLevelTarget = sub * sub;
}

static void UpdateLed() {
  uint32_t now = System::GetNow();

  /* A button press owns the LED for a moment to report what it changed:
     the sub's waveform as a colour, its octave as a brightness. */
  if (now - readoutTime < kReadoutMs) {
    if (readoutIsWaveform) {
      if (subIsSquare)
        hw.led.Set(0.f, 0.85f, 0.85f);
      else
        hw.led.Set(0.90f, 0.75f, 0.40f);
    } else {
      float v = subOctaves == 1 ? 0.20f : 0.90f;
      hw.led.Set(0.55f * v, 0.10f * v, v);
    }
    return;
  }

  /* Otherwise: colour says where the morph is sitting, brightness says how
     hard Weight is working, and the sub washes the whole thing toward white
     as it comes up. */
  float r = 0.f, g = 0.f, b = 0.f;
  for (int w = 0; w < kNumWaves; w++) {
    r += kWaveRgb[w][0] * mixTarget[w];
    g += kWaveRgb[w][1] * mixTarget[w];
    b += kWaveRgb[w][2] * mixTarget[w];
  }
  float value = 0.18f + 0.62f * weightMixTarget;
  float white = 0.25f * subLevelTarget * value;
  hw.led.Set(r * value + white, g * value + white, b * value + white);
}

int main(void) {
  hw.Init();
  float sampleRate = hw.SampleRate();

  /* All four run at the same pitch from the same starting phase. The polyblep
     triangle comes out of an integrated square, so it already sits a quarter
     cycle from a sine started at zero; nudging the sine to "line them up"
     cancels the middle of that leg almost completely. Leave the phases alone. */
  static const uint8_t kWaveform[kNumWaves] = {
      Oscillator::WAVE_SIN, Oscillator::WAVE_POLYBLEP_TRI,
      Oscillator::WAVE_POLYBLEP_SQUARE, Oscillator::WAVE_POLYBLEP_SAW};
  for (int w = 0; w < kNumWaves; w++) {
    osc[w].Init(sampleRate);
    osc[w].SetWaveform(kWaveform[w]);
    osc[w].SetAmp(1.f);
  }

  oscSub.Init(sampleRate);
  oscSub.SetWaveform(Oscillator::WAVE_SIN);
  oscSub.SetAmp(1.f);

  weight.Init();

  // Boot straight onto the panel, so what you see is what you hear.
  ApplyControls();
  freq = freqTarget;
  subFreq = subFreqTarget;
  for (int w = 0; w < kNumWaves; w++) mix[w] = mixTarget[w];
  weightMix = weightMixTarget;
  subLevel = subLevelTarget;

  hw.StartAudio(AudioCallback);

  while (1) {
    hw.ProcessControls();

    if (hw.button[BUTTON_1].RisingEdge()) {
      subOctaves = subOctaves == 1 ? 2 : 1;
      readoutIsWaveform = false;
      readoutTime = System::GetNow();
    }
    if (hw.button[BUTTON_2].RisingEdge()) {
      subIsSquare = !subIsSquare;
      oscSub.SetWaveform(subIsSquare ? Oscillator::WAVE_POLYBLEP_SQUARE
                                     : Oscillator::WAVE_SIN);
      readoutIsWaveform = true;
      readoutTime = System::GetNow();
    }

    ApplyControls();
    UpdateLed();
    System::Delay(1);
  }
}
