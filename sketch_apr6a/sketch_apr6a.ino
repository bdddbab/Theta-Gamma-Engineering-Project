// ── Libraries ──────────────────────────────────────────────────────────────
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ── LCD setup ──────────────────────────────────────────────────────────────
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ── Pin definitions ────────────────────────────────────────────────────────
const int BUTTON_PIN  = 2;   // button/hall sensor
const int VOLTAGE_PIN = A0;  // potentiometer 1 — simulates voltage
const int CURRENT_PIN = A1;  // potentiometer 2 — simulates current

// ── Pulse counting ─────────────────────────────────────────────────────────
volatile int pulseCount = 0;
volatile unsigned long lastPulseTime = 0;

// ── Timing ─────────────────────────────────────────────────────────────────
unsigned long lastCalcTime = 0;
const unsigned long CALC_INTERVAL = 1000; // recalculate every 1 second

// ── Results ────────────────────────────────────────────────────────────────
float rpm     = 0.0;
float voltage = 0.0;
float current = 0.0;
float power   = 0.0;

// ── Generator pole count ───────────────────────────────────────────────────
const int PULSES_PER_REV = 1;

// ── Interrupt ──────────────────────────────────────────────────────────────
void countPulse() {
  unsigned long now = millis();
  if (now - lastPulseTime > 200) {
    pulseCount++;
    lastPulseTime = now;
  }
}

// ── Startup ────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(9600);
  pinMode(BUTTON_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), countPulse, RISING);

  lcd.init();
  lcd.backlight();

  lcd.setCursor(0, 0);
  lcd.print("Generator Mon   ");
  lcd.setCursor(0, 1);
  lcd.print("Starting...     ");
  delay(1500);
  lcd.clear();
}

// ── Main loop ──────────────────────────────────────────────────────────────
void loop() {
  unsigned long now = millis();

  if (now - lastCalcTime >= CALC_INTERVAL) {
    noInterrupts();
    int count = pulseCount;
    pulseCount = 0;
    interrupts();

    // RPM
    rpm = ((float)count / PULSES_PER_REV) * 60.0;

    // Voltage — 0 to 5V range
    // on real hardware multiply by your voltage divider ratio
    voltage = (analogRead(VOLTAGE_PIN) / 1023.0) * 5.0;

    // Current — 0 to 5A range
    // on real hardware map ACS712 output to your current range
    current = (analogRead(CURRENT_PIN) / 1023.0) * 5.0;

    // Power = voltage x current
    power = voltage * current;

    lastCalcTime = now;

    // ── Update LCD ──────────────────────────────────────────────────────
    // Line 1: RPM and voltage — e.g. "RPM:120 V:4.92V"
    lcd.setCursor(0, 0);
    lcd.print("RPM:");
    lcd.print((int)rpm);
    // pad with spaces so old digits get overwritten
    int rpmDigits = rpm >= 1000 ? 4 : rpm >= 100 ? 3 : rpm >= 10 ? 2 : 1;
    int vStart = 4 + rpmDigits + 1; // "RPM:" + digits + space
    for (int i = vStart; i < 7; i++) lcd.print(" ");
    lcd.print("V:");
    lcd.print(voltage, 1);
    lcd.print("V  ");

    // Line 2: current and power — e.g. "I:2.30A W:11.3W"
    lcd.setCursor(0, 1);
    lcd.print("I:");
    lcd.print(current, 1);
    lcd.print("A W:");
    lcd.print(power, 1);
    lcd.print("W  ");

    Serial.print("RPM:"); Serial.print((int)rpm);
    Serial.print(" V:"); Serial.print(voltage, 1);
    Serial.print(" A:"); Serial.print(current, 1);
    Serial.print(" W:"); Serial.println(power, 1);
  }
}