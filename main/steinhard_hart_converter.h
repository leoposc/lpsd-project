#include <math.h>

#define V_SUPPLY 3.3f                // Supply voltage in volts
#define R_SENSOR_NOMINAL 10000.0f    // Series resistor value in ohms
#define R_SERIES 10000.0f            // Nominal resistance of the thermistor at 25 degrees C
#define B_COEFF 3978.0f              // B coefficient of the thermistor
#define TEMP_NOMINAL (25.0f + 273.15f) // Nominal temperature in Kelvin

float apply_steinhart_hart(float r)
{
    float ln_r = log(r);
    float inv_T = (ln_r / B_COEFF) + (1.0f / TEMP_NOMINAL);
    return (1.0f / inv_T) - 273.15f;
}
float compute_temperature_celsius(float v_thermistor)
{
    float v_1 = 3.3f - v_thermistor;
    float r_thermistor = (v_thermistor / v_1) * R_SERIES;
    float r = r_thermistor / R_SENSOR_NOMINAL;
    return apply_steinhart_hart(r);
}
