#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <math.h>

// ======================================================
// SMART SOLAR DRYER - DASHBOARD DEMO MODEL
// ------------------------------------------------------
// IMPORTANT:
// The values below are SIMULATED / DEMO values.
// They are generated inside the ESP32 for dashboard testing.
// They are NOT real sensor measurements.
//
// Model assumptions:
// - Lower zone is close to 100 W heater.
// - Lower temperature stays about 5 to 9 C above upper zone.
// - Lower RH is lower than upper RH.
// - Battery model assumes a 12 V, 26 Ah lead-acid battery.
// - Two small DC fans are assumed at 5 W each.
// - Heater is 100 W and cycles using simple thermostat logic.
// ======================================================

// ======================================================
// WIFI
// ======================================================

const char* ssid = "YOUR_WIFI_NAME";
const char* password = "YOUR_WIFI_PASSWORD";

WebServer server(80);

// ======================================================
// LOAD / BATTERY MODEL
// ======================================================

const float HEATER_POWER_W = 100.0;
const float FAN1_POWER_W   = 5.0;
const float FAN2_POWER_W   = 5.0;

// 12 V x 26 Ah = 312 Wh nominal.
// A modest derating is used for the higher discharge rate.
const float BATTERY_NOMINAL_V = 12.0;
const float BATTERY_AH        = 26.0;
const float BATTERY_DERATING  = 0.85;

const float EFFECTIVE_BATTERY_WH =
  BATTERY_NOMINAL_V * BATTERY_AH * BATTERY_DERATING;

// Approximate internal resistance for demo voltage sag.
const float BATTERY_INTERNAL_R = 0.010;

// Start dashboard at 87% SOC.
float batteryPercent = 87.0;
float batteryEnergyWh =
  EFFECTIVE_BATTERY_WH * (batteryPercent / 100.0);

float batteryVoltage = 12.55;

// Battery display changes very slowly for a smooth dashboard.
// Every 15 seconds, SOC drops by only 0.1% or 0.2%.
unsigned long lastBatteryDisplayUpdate = 0;
const unsigned long BATTERY_DISPLAY_INTERVAL = 15000UL;

// ======================================================
// SIMULATED ENVIRONMENT
// ======================================================

float upperTemp = 34.5;
float lowerTemp = 41.0;

float upperHumidity = 48.0;
float lowerHumidity = 40.0;

bool heaterOn = true;

// Fans are assumed to run continuously during drying.
bool fan1On = true;
bool fan2On = true;

float loadPowerW = 0.0;
float loadCurrentA = 0.0;

unsigned long lastModelUpdate = 0;

// ======================================================
// DASHBOARD
// ======================================================

const char webpage[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Smart Solar Dryer</title>

<style>
* { box-sizing: border-box; }

body {
  margin: 0;
  font-family: Arial, Helvetica, sans-serif;
  background: #f2f7f5;
  color: #17352a;
}

.header {
  background: linear-gradient(135deg,#087f5b,#20a36a);
  color: white;
  padding: 25px;
  text-align: center;
  box-shadow: 0 3px 10px rgba(0,0,0,0.15);
}

.header h1 {
  margin: 0;
  font-size: 28px;
}

.header p {
  margin: 7px 0 0;
  font-size: 14px;
}

.container {
  max-width: 900px;
  margin: auto;
  padding: 20px;
}


.cards {
  display: grid;
  grid-template-columns: repeat(2,1fr);
  gap: 18px;
}

.card {
  background: white;
  border-radius: 18px;
  padding: 28px 15px;
  text-align: center;
  box-shadow: 0 4px 15px rgba(0,0,0,0.10);
}

.title {
  font-size: 14px;
  font-weight: bold;
  color: #68746f;
  margin-bottom: 15px;
}

.value {
  font-size: 42px;
  font-weight: bold;
  color: #087f5b;
}

.unit {
  font-size: 18px;
  color: #52605a;
}

.batteryOuter {
  width: 80%;
  height: 22px;
  margin: 15px auto 5px auto;
  background: #e4e8e6;
  border-radius: 12px;
  overflow: hidden;
}

.batteryFill {
  height: 100%;
  width: 0%;
  background: linear-gradient(90deg,#ff922b,#20a36a);
  transition: width 1s;
}

.small {
  margin-top: 10px;
  font-size: 14px;
  color: #52605a;
}

.status {
  margin-top: 20px;
  text-align: center;
  font-size: 13px;
  color: #087f5b;
  font-weight: bold;
}

.footer {
  text-align: center;
  margin-top: 25px;
  font-size: 12px;
  color: #7c8983;
}

@media(max-width:650px) {
  .cards {
    grid-template-columns: 1fr;
  }
}
</style>
</head>

<body>

<div class="header">
  <h1>SMART SOLAR DRYER</h1>
  <p>Environmental & Battery Monitoring</p>
</div>

<div class="container">

  <div class="cards">

    <div class="card">
      <div class="title">UPPER TEMPERATURE</div>
      <span class="value" id="upperTemp">--</span>
      <span class="unit">&deg;C</span>
    </div>

    <div class="card">
      <div class="title">LOWER TEMPERATURE</div>
      <span class="value" id="lowerTemp">--</span>
      <span class="unit">&deg;C</span>
    </div>

    <div class="card">
      <div class="title">UPPER HUMIDITY</div>
      <span class="value" id="upperHumidity">--</span>
      <span class="unit">%</span>
    </div>

    <div class="card">
      <div class="title">LOWER HUMIDITY</div>
      <span class="value" id="lowerHumidity">--</span>
      <span class="unit">%</span>
    </div>

    <div class="card">
      <div class="title">BATTERY STATUS</div>

      <span class="value" id="battery">--</span>
      <span class="unit">%</span>

      <div class="batteryOuter">
        <div class="batteryFill" id="batteryFill"></div>
      </div>

      <div class="small">
        <b><span id="voltage">--</span> V</b>
      </div>

      <div class="small">
        Estimated remaining energy: <b><span id="energy">--</span> Wh</b>
      </div>
    </div>

    <div class="card">
      <div class="title">SYSTEM LOAD</div>

      <span class="value" id="power">--</span>
      <span class="unit">W</span>

      <div class="small">
        Current: <span id="current">--</span> A
      </div>

      <div class="small">
        Heater: <span id="heater">--</span>
      </div>
    </div>

  </div>

  <div class="status">
    ESP32 Wi-Fi Dashboard Online
  </div>

  <div class="footer">
    Smart Solar-Powered Dryer Monitoring System
  </div>

</div>

<script>
function updateDashboard() {

  fetch('/data')
  .then(response => response.json())
  .then(data => {

    document.getElementById("upperTemp").innerHTML = data.upperTemp;
    document.getElementById("lowerTemp").innerHTML = data.lowerTemp;

    document.getElementById("upperHumidity").innerHTML = data.upperHumidity;
    document.getElementById("lowerHumidity").innerHTML = data.lowerHumidity;

    document.getElementById("battery").innerHTML = data.battery;
    document.getElementById("voltage").innerHTML = data.voltage;
    document.getElementById("energy").innerHTML = data.energy;

    document.getElementById("batteryFill").style.width =
      data.battery + "%";

    document.getElementById("power").innerHTML = data.power;
    document.getElementById("current").innerHTML = data.current;
    document.getElementById("heater").innerHTML = data.heater;
  })
  .catch(error => console.log(error));
}

setInterval(updateDashboard, 1500);
updateDashboard();
</script>

</body>
</html>
)rawliteral";

// ======================================================
// HELPERS
// ======================================================

float clampValue(float value, float minimumValue, float maximumValue) {
  if (value < minimumValue) return minimumValue;
  if (value > maximumValue) return maximumValue;
  return value;
}

// Approximate 12 V lead-acid open-circuit voltage from SOC.
// This is a dashboard model, not a battery-management measurement.
float leadAcidOCV(float soc) {

  soc = clampValue(soc, 0.0, 100.0);

  if (soc >= 90.0)
    return 12.62 + (soc - 90.0) * (12.73 - 12.62) / 10.0;

  if (soc >= 80.0)
    return 12.50 + (soc - 80.0) * (12.62 - 12.50) / 10.0;

  if (soc >= 70.0)
    return 12.42 + (soc - 70.0) * (12.50 - 12.42) / 10.0;

  if (soc >= 60.0)
    return 12.32 + (soc - 60.0) * (12.42 - 12.32) / 10.0;

  if (soc >= 50.0)
    return 12.20 + (soc - 50.0) * (12.32 - 12.20) / 10.0;

  if (soc >= 40.0)
    return 12.10 + (soc - 40.0) * (12.20 - 12.10) / 10.0;

  if (soc >= 30.0)
    return 12.00 + (soc - 30.0) * (12.10 - 12.00) / 10.0;

  if (soc >= 20.0)
    return 11.90 + (soc - 20.0) * (12.00 - 11.90) / 10.0;

  return 11.75 + soc * (11.90 - 11.75) / 20.0;
}

// ======================================================
// PHYSICS-INFORMED DEMO MODEL
// ======================================================

void updateDemoModel() {

  unsigned long now = millis();

  if (lastModelUpdate == 0) {
    lastModelUpdate = now;
    return;
  }

  float dtSeconds = (now - lastModelUpdate) / 1000.0;

  // Run model every ~1 second.
  if (dtSeconds < 1.0) return;

  lastModelUpdate = now;

  // ----------------------------------------------------
  // HEATER CYCLING
  // ----------------------------------------------------
  // Lower sensor is close to heater.
  // Simple hysteresis keeps the local hot-zone temperature
  // in a plausible range.
  // ----------------------------------------------------

  if (lowerTemp <= 41.5) {
    heaterOn = true;
  }

  if (lowerTemp >= 45.5) {
    heaterOn = false;
  }

  // ----------------------------------------------------
  // UPPER TEMPERATURE
  // ----------------------------------------------------

  float upperTarget;

  if (heaterOn) {
    upperTarget = 37.0;
  } else {
    upperTarget = 34.8;
  }

  upperTemp += (upperTarget - upperTemp) * 0.018 * dtSeconds;

  upperTemp += random(-2, 3) / 100.0;

  upperTemp = clampValue(upperTemp, 33.5, 38.5);

  // ----------------------------------------------------
  // LOWER TEMPERATURE
  // ----------------------------------------------------
  // Keep lower sensor approximately 5-9 C hotter because
  // it is located nearer the heater.
  // ----------------------------------------------------

  float desiredDifference;

  if (heaterOn) {
    desiredDifference = 7.5;
  } else {
    desiredDifference = 5.5;
  }

  desiredDifference += random(-5, 6) / 10.0;

  desiredDifference = clampValue(desiredDifference, 5.0, 9.0);

  float lowerTarget =
    upperTemp + desiredDifference;

  lowerTemp +=
    (lowerTarget - lowerTemp) *
    0.06 * dtSeconds;

  lowerTemp +=
    random(-2, 3) / 100.0;

  // Ensure the requested spatial difference remains realistic.
  float difference =
    lowerTemp - upperTemp;

  if (difference < 5.0) {
    lowerTemp = upperTemp + 5.0;
  }

  if (difference > 9.0) {
    lowerTemp = upperTemp + 9.0;
  }

  // ----------------------------------------------------
  // HUMIDITY - EMPTY DRYER / NO WET AGARBATTI LOAD
  // ----------------------------------------------------
  // No wet product is present, so there is no continuous
  // moisture release into the chamber.
  //
  // Therefore:
  // - Upper-zone RH stays near moderate ambient chamber RH.
  // - Lower-zone RH is lower because it is closer to the heater.
  // - RH changes only slowly, with small realistic fluctuations.
  // ----------------------------------------------------

  float upperHumidityTarget =
    49.0 - (upperTemp - 34.0) * 1.0;

  upperHumidityTarget +=
    random(-5, 6) / 10.0;

  upperHumidity +=
    (upperHumidityTarget - upperHumidity) *
    0.025 * dtSeconds;

  upperHumidity =
    clampValue(upperHumidity, 42.0, 52.0);

  float temperatureDifference =
    lowerTemp - upperTemp;

  // With no wet load, the hotter lower zone remains drier.
  float humidityDifference =
    0.9 * temperatureDifference;

  humidityDifference +=
    random(-3, 4) / 10.0;

  humidityDifference =
    clampValue(humidityDifference, 5.0, 9.0);

  float lowerHumidityTarget =
    upperHumidity - humidityDifference;

  lowerHumidity +=
    (lowerHumidityTarget - lowerHumidity) *
    0.035 * dtSeconds;

  lowerHumidity =
    clampValue(lowerHumidity, 34.0, 46.0);

  // ----------------------------------------------------
  // ELECTRICAL LOAD
  // ----------------------------------------------------

  loadPowerW = 0.0;

  if (fan1On) {
    loadPowerW += FAN1_POWER_W;
  }

  if (fan2On) {
    loadPowerW += FAN2_POWER_W;
  }

  if (heaterOn) {
    loadPowerW += HEATER_POWER_W;
  }

  // Estimate current using nominal battery voltage.
  loadCurrentA =
    loadPowerW / BATTERY_NOMINAL_V;

  // ----------------------------------------------------
  // BATTERY STATUS - SLOW DASHBOARD VARIATION
  // ----------------------------------------------------
  // For the dashboard demo, battery percentage changes only
  // once every 15 seconds, by 0.1% or 0.2%.
  //
  // This gives a smooth and believable visual trend instead
  // of changing too rapidly on screen.
  // ----------------------------------------------------

  if (millis() - lastBatteryDisplayUpdate >= BATTERY_DISPLAY_INTERVAL) {

    lastBatteryDisplayUpdate = millis();

    // If heater is ON, battery tends to drop a little faster.
    if (heaterOn) {
      batteryPercent -= (random(0, 2) == 0) ? 0.1 : 0.2;
    }
    else {
      // Fans only: mostly 0.1% drop.
      batteryPercent -= 0.1;
    }

    batteryPercent =
      clampValue(batteryPercent, 0.0, 100.0);

    // Voltage follows battery SOC slowly.
    float openCircuitVoltage =
      leadAcidOCV(batteryPercent);

    float voltageSag =
      loadCurrentA *
      BATTERY_INTERNAL_R;

    batteryVoltage =
      openCircuitVoltage - voltageSag;

    batteryVoltage =
      clampValue(batteryVoltage, 10.8, 12.8);
  }
}

// ======================================================
// SEND DASHBOARD DATA
// ======================================================

void sendData() {

  String json = "{";

  json += "\"upperTemp\":" + String(upperTemp, 1) + ",";
  json += "\"lowerTemp\":" + String(lowerTemp, 1) + ",";

  json += "\"upperHumidity\":" + String(upperHumidity, 1) + ",";
  json += "\"lowerHumidity\":" + String(lowerHumidity, 1) + ",";

  json += "\"battery\":" + String(batteryPercent, 1) + ",";
  json += "\"voltage\":" + String(batteryVoltage, 2) + ",";
  json += "\"energy\":" + String(EFFECTIVE_BATTERY_WH * batteryPercent / 100.0, 1) + ",";

  json += "\"power\":" + String(loadPowerW, 0) + ",";
  json += "\"current\":" + String(loadCurrentA, 2) + ",";

  json += "\"heater\":\"";
  json += heaterOn ? "ON" : "OFF";
  json += "\"";

  json += "}";

  server.send(200, "application/json", json);
}

// ======================================================
// DASHBOARD
// ======================================================

void showDashboard() {
  server.send_P(200, "text/html", webpage);
}

// ======================================================
// WIFI
// ======================================================

void connectWiFi() {

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }

  // Fixed local dashboard name:
  // http://smartdryer.local/
  MDNS.begin("smartdryer");
  MDNS.addService("http", "tcp", 80);
}

// ======================================================
// SETUP
// ======================================================

void setup() {

  delay(1000);

  randomSeed(micros());

  connectWiFi();

  server.on("/", showDashboard);
  server.on("/data", sendData);

  server.begin();

}

// ======================================================
// LOOP
// ======================================================

void loop() {

  server.handleClient();

  updateDemoModel();


  // ----------------------------------------------------
  // WIFI RECONNECT
  // ----------------------------------------------------

  if (WiFi.status() != WL_CONNECTED) {

    static unsigned long reconnectTimer = 0;

    if (millis() - reconnectTimer >= 10000) {

      reconnectTimer = millis();

      WiFi.disconnect();
      WiFi.begin(ssid, password);
    }
  }
}
