// ============================================================
// Pedal Generator Monitor
// Displays voltage, current, power, and RPM on 16x2 I2C LCD
// Hardware: Arduino Uno, INA226, Hall sensor, LCD 16x2 I2C
// ============================================================
//
// WIRING SUMMARY
// INA226:    SDA->A4  SCL->A5  VCC->5V  GND->GND
//            VIN+ from buck converter output positive
//            VIN- toward power bank positive
// LCD:       SDA->A4  SCL->A5  VCC->5V  GND->GND
// Hall:      VCC->5V  GND->GND  OUT->pin 2
// Magnet:    glued to motor shaft, 2-3mm from hall sensor
//
// LIBRARIES NEEDED (install via Arduino Library Manager)
// - LiquidCrystal_I2C  by Frank de Brabander
// - INA226             by Korneliusz Jarzebski

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <INA226.h>

// ---- addresses ----
#define LCD_ADDRESS  0x27
#define INA_ADDRESS  0x40

// ---- hall sensor ----
#define HALL_PIN     2

// ---- timing ----
#define AVERAGE_INTERVAL  1000   // ms — how often averages are recalculated
#define SERIAL_INTERVAL   1000   // ms between serial prints (matches average window)
#define LCD_INTERVAL       200   // ms between LCD refreshes (smooth feel, no flicker)

// ---- RPM calculation ----
#define MAGNETS_PER_REV  1

// ============================================================

LiquidCrystal_I2C lcd(LCD_ADDRESS, 16, 2);
INA226 ina;

// ---- hall sensor (interrupt) ----
volatile unsigned long lastPulseTime  = 0;
volatile unsigned long pulseInterval  = 0;
volatile bool          newPulse       = false;

// ---- 1-second accumulator ----
float    sumVoltage  = 0.0;
float    sumCurrent  = 0.0;
float    sumPower    = 0.0;
float    sumRpm      = 0.0;
int      sampleCount = 0;

// ---- published averages (updated once per second) ----
float avgVoltage = 0.0;
float avgCurrent = 0.0;
float avgPower   = 0.0;
float avgRpm     = 0.0;

unsigned long lastAverageTime = 0;
unsigned long lastSerialPrint = 0;
unsigned long lastLcdUpdate   = 0;

// ============================================================
void hallInterrupt() {
  unsigned long now = micros();
  if (lastPulseTime > 0) {
    pulseInterval = now - lastPulseTime;
  }
  lastPulseTime = now;
  newPulse = true;
}

// ============================================================
void setup() {
  Serial.begin(9600);
  Wire.begin();

  // --- LCD setup ---
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Pedal Generator");
  lcd.setCursor(0, 1);
  lcd.print("Starting up...");
  delay(2000);
  lcd.clear();

  // --- INA226 setup ---
  if (!ina.begin(INA_ADDRESS)) {
    Serial.println("INA226 not found - check wiring and address");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("INA226 missing!");
    lcd.setCursor(0, 1);
    lcd.print("Check wiring");
    while (true);
  }

  ina.configure(
    INA226_AVERAGES_16,
    INA226_BUS_CONV_TIME_1100US,
    INA226_SHUNT_CONV_TIME_1100US,
    INA226_MODE_SHUNT_BUS_CONT
  );
  ina.calibrate(0.1, 3.0);

  Serial.println("INA226 ready");

  // --- Hall sensor setup ---
  pinMode(HALL_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(HALL_PIN), hallInterrupt, FALLING);

  Serial.println("System ready");
  Serial.println("Volt(V) | Curr(A) | Power(W) | RPM");
  Serial.println("--------|---------|----------|----");
}

// ============================================================
void loop() {
  unsigned long now = millis();

  // ---- read instantaneous values ----
  float voltage = ina.readBusVoltage();
  float current = ina.readShuntCurrent();
  float power   = ina.readBusPower();

  if (current < 0.0) current = 0.0;
  if (power   < 0.0) power   = 0.0;

  // ---- calculate instantaneous RPM ----
  float rpm = 0.0;
  if (pulseInterval > 0) {
    unsigned long timeSinceLastPulse = micros() - lastPulseTime;
    if (timeSinceLastPulse > 2000000UL) {
      // shaft has stopped — reset so next reading starts fresh
      rpm = 0.0;
      pulseInterval = 0;
    } else {
      rpm = 60000000.0 / ((float)pulseInterval * MAGNETS_PER_REV);
    }
  }

  // ---- accumulate samples ----
  sumVoltage += voltage;
  sumCurrent += current;
  sumPower   += power;
  sumRpm     += rpm;
  sampleCount++;

  // ---- every 1 second: publish averages and reset accumulators ----
  if (now - lastAverageTime >= AVERAGE_INTERVAL) {
    lastAverageTime = now;

    if (sampleCount > 0) {
      avgVoltage = sumVoltage / sampleCount;
      avgCurrent = sumCurrent / sampleCount;
      avgPower   = sumPower   / sampleCount;
      avgRpm     = sumRpm     / sampleCount;
    }

    // reset
    sumVoltage  = 0.0;
    sumCurrent  = 0.0;
    sumPower    = 0.0;
    sumRpm      = 0.0;
    sampleCount = 0;
  }

  // ---- update LCD every LCD_INTERVAL ms ----
  // static display: row 0 = voltage + current, row 1 = power + RPM
  if (now - lastLcdUpdate >= LCD_INTERVAL) {
    lastLcdUpdate = now;

    // Row 0: "V:12.34V  I:1.234A" — 19 chars, fits 16x2 tight so we shorten units
    // Format: "V:XX.XXV I:X.XXXA"  (16 chars exactly)
    lcd.setCursor(0, 0);
    lcd.print("V:");
    lcd.print(avgVoltage, 2);   // e.g. "12.34"
    lcd.print("V ");
    lcd.print("I:");
    lcd.print(avgCurrent, 3);   // e.g. "1.234"
    lcd.print("A");

    // Row 1: "P:XX.XW  R:XXXrpm"
    // Power takes up to 5 chars (XX.XW), RPM up to 6 (XXXrpm)
    lcd.setCursor(0, 1);
    lcd.print("P:");
    lcd.print(avgPower, 1);     // e.g. "15.2"
    lcd.print("W ");
    lcd.print("R:");
    lcd.print((int)avgRpm);
    lcd.print("rpm  ");         // trailing spaces clear leftover chars on drop
  }

  // ---- serial output ----
  if (now - lastSerialPrint >= SERIAL_INTERVAL) {
    lastSerialPrint = now;
    Serial.print(avgVoltage, 3); Serial.print("V  |  ");
    Serial.print(avgCurrent, 4); Serial.print("A  |  ");
    Serial.print(avgPower, 3);   Serial.print("W  |  ");
    Serial.print((int)avgRpm);   Serial.println(" RPM");
  }
}