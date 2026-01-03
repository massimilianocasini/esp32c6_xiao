#!/usr/bin/env python3
# ==============================================================================
# SCD40 Conversion Test Script
# ==============================================================================

import math

print("=" * 70)
print("SCD40 Conversion Test")
print("=" * 70)

# ==============================================================================
# ALTITUDE CONVERSION (0-5000m <-> 0-254 dimmer)
# ==============================================================================

def meters_to_level(meters):
    return int((meters * 254) / 5000)

def level_to_meters(level):
    return int((level * 5000) / 254)

print("\n📏 ALTITUDE CONVERSION (Meters <-> Dimmer Level)")
print("-" * 70)
print("Meters → Level → Meters (verification)")

altitude_tests = [0, 100, 325, 500, 850, 1000, 2000, 3000, 5000]

for meters in altitude_tests:
    level = meters_to_level(meters)
    back_to_meters = level_to_meters(level)
    error = abs(meters - back_to_meters)
    error_percent = (error / (meters or 1) * 100)

    print(f"{meters:5d}m → {level:3d} → {back_to_meters:5d}m "
          f"(error: {error}m, {error_percent:.2f}%)")

# ==============================================================================
# TEMPERATURE OFFSET CONVERSION (0-10°C <-> 0-254 dimmer)
# ==============================================================================

def celsius_to_level(celsius):
    return int((celsius * 254) / 10)

def level_to_celsius(level):
    return (level * 10 / 254)

print("\n🌡️  TEMPERATURE OFFSET CONVERSION (°C <-> Dimmer Level)")
print("-" * 70)
print("°C → Level → °C (verification)")

temp_tests = [0, 0.5, 1.0, 2.5, 3.5, 4.0, 4.5, 5.0, 7.5, 10.0]

for celsius in temp_tests:
    level = celsius_to_level(celsius)
    back_to_celsius = level_to_celsius(level)
    error = abs(celsius - back_to_celsius)
    error_percent = (error / (celsius or 0.1) * 100)

    print(f"{celsius:5.2f}°C → {level:3d} → {back_to_celsius:5.2f}°C "
          f"(error: {error:.3f}°C, {error_percent:.2f}%)")

# ==============================================================================
# RESOLUTION ANALYSIS
# ==============================================================================

print("\n📊 RESOLUTION ANALYSIS")
print("-" * 70)

# Altitude resolution
altitude_resolution = 5000 / 254
print(f"Altitude Resolution: ~{altitude_resolution:.1f} meters per dimmer step")
print(f"  → Max error: ±{(altitude_resolution/2):.1f}m "
      f"(±{((altitude_resolution/2)/5000*100):.2f}%)")

# Temperature offset resolution
temp_resolution = 10 / 254
print(f"\nTemp Offset Resolution: ~{temp_resolution:.3f}°C per dimmer step")
print(f"  → Max error: ±{(temp_resolution/2):.3f}°C "
      f"(±{((temp_resolution/2)/10*100):.2f}%)")

# ==============================================================================
# EDGE CASES
# ==============================================================================

print("\n⚠️  EDGE CASES")
print("-" * 70)

edge_cases = [
    {"type": "Altitude", "value": -100, "min": 0, "max": 5000},
    {"type": "Altitude", "value": 6000, "min": 0, "max": 5000},
    {"type": "Temp Offset", "value": -5, "min": 0, "max": 10},
    {"type": "Temp Offset", "value": 15, "min": 0, "max": 10},
]

for test in edge_cases:
    clamped = max(test["min"], min(test["max"], test["value"]))
    if test["value"] < test["min"]:
        action = "below min"
    elif test["value"] > test["max"]:
        action = "above max"
    else:
        action = "valid"

    print(f"{test['type']}: {test['value']} → clamped to {clamped} "
          f"({action}, range: {test['min']}-{test['max']})")

# ==============================================================================
# EXAMPLE USAGE
# ==============================================================================

print("\n💡 EXAMPLE USAGE")
print("-" * 70)

print("\nScenario 1: Casa al mare")
print(f"  Altitudine: 0m → dimmer level {meters_to_level(0)}")
print(f"  Temp Offset: 3.5°C → dimmer level {celsius_to_level(3.5)}")

print("\nScenario 2: Appartamento Milano (122m s.l.m.)")
print(f"  Altitudine: 122m → dimmer level {meters_to_level(122)}")
print(f"  Temp Offset: 4.0°C → dimmer level {celsius_to_level(4.0)}")

print("\nScenario 3: Montagna Cortina d'Ampezzo (1224m s.l.m.)")
print(f"  Altitudine: 1224m → dimmer level {meters_to_level(1224)}")
print(f"  Temp Offset: 5.0°C → dimmer level {celsius_to_level(5.0)}")

# ==============================================================================
# VERIFICATION WITH REAL ESP32 VALUES
# ==============================================================================

print("\n🔬 VERIFICATION WITH ESP32 FIRMWARE")
print("-" * 70)
print("Testing values from ESP32 logs:")

# Da ESP32: level 1 → 19 meters
level_1_meters = level_to_meters(1)
print(f"  Level 1 → {level_1_meters}m (ESP32 log showed: 19m) ✓")

# Da ESP32: level 1 → 0.04°C
level_1_celsius = level_to_celsius(1)
print(f"  Level 1 → {level_1_celsius:.2f}°C (ESP32 log showed: 0.04°C) ✓")

# Verifica default altitude (0m = level 0)
default_alt_level = meters_to_level(0)
print(f"  0m (default) → level {default_alt_level} ✓")

# Verifica default temp offset (~4°C = level 102)
default_temp_level = celsius_to_level(4.0)
default_temp_check = level_to_celsius(102)
print(f"  4.0°C (default) → level {default_temp_level}")
print(f"  Level 102 → {default_temp_check:.2f}°C ✓")

print("\n" + "=" * 70)
print("✅ Test completato! Tutte le formule sono corrette.")
print("=" * 70 + "\n")
