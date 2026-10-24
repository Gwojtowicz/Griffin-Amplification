#pragma once

// Tube Lab bench (1.58.0): the things an amp tech changes on the bench rather than on the front panel.
//
// * Variac: the wall voltage into the power transformer (120 V stock). Everything the transformer
//   makes follows it: B+ (so the preamp, phase inverter and power tubes all lose or gain headroom),
//   the fixed-bias supply (it comes off the same transformer, so the power tubes stay roughly as hot
//   as they were), and the heaters. The supplies follow within a fraction of a second; the cathodes
//   take seconds to cool or warm (see kHeaterTauS), as on a real amp.
// * Heaters and emission: an oxide cathode emits by temperature (Richardson's law,
//   J ~ T^2 exp(-W / kT)), and its temperature goes with the heater power, about T ~ V^0.5. A healthy
//   cathode can emit several times the most current the circuit ever asks of it, so normally emission
//   doesn't limit anything. Turn the heaters down (or wear the tubes out) and that reserve shrinks
//   until the peaks run into it: a soft ceiling on the tube current, i.e. compressed, rounded attacks.
// * Tube age: a worn cathode has less active area left -- less perveance (gain, current) -- and much
//   less emission reserve.
// * Bias pot (power tubes, PowerAmp.h): sets the idle current at stock mains with new tubes, as you'd
//   set it on the bench. Worn tubes or a lowered Variac then idle where the circuit puts them.
//
// Stock (120 V, new tubes, bias at 100%) is exactly the sound without the bench: every part of it is
// skipped.

#include <algorithm>
#include <cmath>

namespace tube
{
namespace bench
{
constexpr double kMainsNominal = 120.0;
constexpr double kHeaterNominal = 6.3;
constexpr double kCathodeK0 = 1050.0; // Oxide cathode at its rated heater voltage (K)
constexpr double kWorkK = 7000.0; // The cathode's effective work function over Boltzmann's constant (K)
constexpr double kNewReserve = 8.0; // A new tube's emission over the most current its circuit asks for
constexpr double kAgeReserveLoss = 0.85; // A worn-out tube keeps 15% of its reserve...
constexpr double kAgePerveanceLoss = 0.3; // ...and 70% of its perveance
constexpr double kHeaterTauS = 4.0; // Cathode temperature lag behind the heater voltage
constexpr double kSupplyTauS = 0.25; // The supplies following the Variac (the knob's own glide)

// Cathode temperature (K) for the heater voltage as a ratio of its rating.
inline double CathodeK(double heaterRatio) { return kCathodeK0 * std::sqrt(std::max(heaterRatio, 0.05)); }

// Emission available, relative to the rated heater voltage (Richardson's law).
inline double EmissionRatio(double heaterRatio)
{
  const double t = CathodeK(heaterRatio);
  const double r = t / kCathodeK0;
  return r * r * std::exp(kWorkK / kCathodeK0 - kWorkK / t);
}

// Emission reserve: how many times the circuit's largest current the cathode can still supply.
inline double Reserve(double heaterRatio, double age)
{
  return kNewReserve * (1.0 - kAgeReserveLoss * std::clamp(age, 0.0, 1.0)) * EmissionRatio(heaterRatio);
}

inline double Perveance(double age) { return 1.0 - kAgePerveanceLoss * std::clamp(age, 0.0, 1.0); }

// The emission ceiling on a current i (amps; invCeiling = 1 / the ceiling): i / (1 + (i/c)^4)^(1/4),
// a soft knee that leaves currents well under the ceiling alone. Returns the limited current and
// sets slope to its derivative with respect to i, which is (1 + (i/c)^4)^(-5/4).
// It runs inside the circuits' Newton loops, where two square roots and a division cost the Tube
// Lab ~40% more CPU; most currents sit well under the ceiling (x = (i/c)^4 < 0.05), where a cubic
// series is exact to under 1e-6 (-120 dB), so only the rest pay for the exact form.
inline double Ceiling(double i, double invCeiling, double& slope)
{
  const double r = std::max(i, 0.0) * invCeiling;
  const double r2 = r * r;
  const double x = r2 * r2;
  if (x < 0.05)
  {
    // (1 + x)^(-1/4) and (1 + x)^(-5/4), to x^3.
    slope = 1.0 + x * (-1.25 + x * (1.40625 - x * 1.5234375));
    return i * (1.0 + x * (-0.25 + x * (0.15625 - x * 0.1171875)));
  }
  const double q = 1.0 / std::sqrt(std::sqrt(1.0 + x)); // (1 + x)^(-1/4)
  const double q2 = q * q;
  slope = q2 * q2 * q;
  return i * q;
}

// What the circuits are given: mains relative to 120 V, perveance and the emission ceiling's inverse
// (0 = none).
struct State
{
  double mains = 1.0;
  double perveance = 1.0;
  double invCeiling = 0.0;
  bool Stock() const { return mains == 1.0 && perveance == 1.0 && invCeiling == 0.0; }
};
} // namespace bench
} // namespace tube
