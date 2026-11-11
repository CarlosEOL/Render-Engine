#pragma once

namespace Astronomy {
    constexpr double metresPerAu = 149597870700.0;
    constexpr double secondsPerDay = 86400.0;
    constexpr double secondsPerYear = 31557600.0;
    constexpr double solarRadiusAu = 695700000.0 / metresPerAu;
    constexpr double earthRadiusAu = 6371000.0 / metresPerAu;
    constexpr double solarGravityAu = 1.32712440018e20 / (metresPerAu * metresPerAu * metresPerAu);
    constexpr double lightSpeedAu = 299792458.0 / metresPerAu;
    constexpr double auPerLightYear = lightSpeedAu * secondsPerYear;
    constexpr double gargantuaMassSolar = 1.0e8;
    constexpr double gargantuaGravityAu = solarGravityAu * gargantuaMassSolar;
    constexpr double gargantuaHorizonAu = 2 * gargantuaGravityAu / (lightSpeedAu * lightSpeedAu);
    constexpr double millerOrbitAu = 3.1 * gargantuaHorizonAu;
    constexpr double millerRadiusAu = earthRadiusAu;
    constexpr double observerLengthMetres = 13.2;
    constexpr double observerWidthMetres = 4.2;
    constexpr double observerClearanceAu = observerLengthMetres * .5 / metresPerAu;
}
