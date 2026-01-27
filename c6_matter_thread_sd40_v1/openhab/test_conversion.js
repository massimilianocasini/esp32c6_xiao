// ==============================================================================
// SCD40 Conversion Test Script
// ==============================================================================
// Testa le formule di conversione senza OpenHAB
//
// Esegui con: node test_conversion.js
// ==============================================================================

console.log("=".repeat(70));
console.log("SCD40 Conversion Test");
console.log("=".repeat(70));

// ==============================================================================
// ALTITUDE CONVERSION (0-5000m <-> 0-254 dimmer)
// ==============================================================================

function metersToLevel(meters) {
    return Math.floor((meters * 254) / 5000);
}

function levelToMeters(level) {
    return Math.floor((level * 5000) / 254);
}

console.log("\n📏 ALTITUDE CONVERSION (Meters <-> Dimmer Level)");
console.log("-".repeat(70));
console.log("Meters → Level → Meters (verification)");

const altitudeTests = [0, 100, 325, 500, 850, 1000, 2000, 3000, 5000];

altitudeTests.forEach(meters => {
    const level = metersToLevel(meters);
    const backToMeters = levelToMeters(level);
    const error = Math.abs(meters - backToMeters);
    const errorPercent = (error / (meters || 1) * 100).toFixed(2);

    console.log(
        `${meters.toString().padStart(5)}m → ` +
        `${level.toString().padStart(3)} → ` +
        `${backToMeters.toString().padStart(5)}m ` +
        `(error: ${error}m, ${errorPercent}%)`
    );
});

// ==============================================================================
// TEMPERATURE OFFSET CONVERSION (0-10°C <-> 0-254 dimmer)
// ==============================================================================

function celsiusToLevel(celsius) {
    return Math.floor((celsius * 254) / 10);
}

function levelToCelsius(level) {
    return (level * 10 / 254);
}

console.log("\n🌡️  TEMPERATURE OFFSET CONVERSION (°C <-> Dimmer Level)");
console.log("-".repeat(70));
console.log("°C → Level → °C (verification)");

const tempTests = [0, 0.5, 1.0, 2.5, 3.5, 4.0, 4.5, 5.0, 7.5, 10.0];

tempTests.forEach(celsius => {
    const level = celsiusToLevel(celsius);
    const backToCelsius = levelToCelsius(level);
    const error = Math.abs(celsius - backToCelsius);
    const errorPercent = (error / (celsius || 0.1) * 100).toFixed(2);

    console.log(
        `${celsius.toFixed(2).padStart(5)}°C → ` +
        `${level.toString().padStart(3)} → ` +
        `${backToCelsius.toFixed(2).padStart(5)}°C ` +
        `(error: ${error.toFixed(3)}°C, ${errorPercent}%)`
    );
});

// ==============================================================================
// RESOLUTION ANALYSIS
// ==============================================================================

console.log("\n📊 RESOLUTION ANALYSIS");
console.log("-".repeat(70));

// Altitude resolution
const altitudeResolution = 5000 / 254;
console.log(`Altitude Resolution: ~${altitudeResolution.toFixed(1)} meters per dimmer step`);
console.log(`  → Max error: ±${(altitudeResolution/2).toFixed(1)}m (±${((altitudeResolution/2)/5000*100).toFixed(2)}%)`);

// Temperature offset resolution
const tempResolution = 10 / 254;
console.log(`\nTemp Offset Resolution: ~${tempResolution.toFixed(3)}°C per dimmer step`);
console.log(`  → Max error: ±${(tempResolution/2).toFixed(3)}°C (±${((tempResolution/2)/10*100).toFixed(2)}%)`);

// ==============================================================================
// EDGE CASES
// ==============================================================================

console.log("\n⚠️  EDGE CASES");
console.log("-".repeat(70));

// Out of range tests
const edgeCases = [
    { type: "Altitude", value: -100, min: 0, max: 5000 },
    { type: "Altitude", value: 6000, min: 0, max: 5000 },
    { type: "Temp Offset", value: -5, min: 0, max: 10 },
    { type: "Temp Offset", value: 15, min: 0, max: 10 },
];

edgeCases.forEach(test => {
    const clamped = Math.max(test.min, Math.min(test.max, test.value));
    const action = test.value < test.min ? "below min" :
                   test.value > test.max ? "above max" : "valid";

    console.log(
        `${test.type}: ${test.value} → ` +
        `clamped to ${clamped} (${action}, range: ${test.min}-${test.max})`
    );
});

// ==============================================================================
// EXAMPLE USAGE
// ==============================================================================

console.log("\n💡 EXAMPLE USAGE");
console.log("-".repeat(70));

console.log("\nScenario 1: Casa al mare");
console.log(`  Altitudine: 0m → dimmer level ${metersToLevel(0)}`);
console.log(`  Temp Offset: 3.5°C → dimmer level ${celsiusToLevel(3.5)}`);

console.log("\nScenario 2: Appartamento Milano (122m s.l.m.)");
console.log(`  Altitudine: 122m → dimmer level ${metersToLevel(122)}`);
console.log(`  Temp Offset: 4.0°C → dimmer level ${celsiusToLevel(4.0)}`);

console.log("\nScenario 3: Montagna Cortina d'Ampezzo (1224m s.l.m.)");
console.log(`  Altitudine: 1224m → dimmer level ${metersToLevel(1224)}`);
console.log(`  Temp Offset: 5.0°C → dimmer level ${celsiusToLevel(5.0)}`);

console.log("\n" + "=".repeat(70));
console.log("Test completato!");
console.log("=".repeat(70) + "\n");
