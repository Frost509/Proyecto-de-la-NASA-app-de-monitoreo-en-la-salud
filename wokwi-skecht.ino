#include <WiFi.h>
#include <PubSubClient.h>

const char* ssid = "Wokwi-GUEST";
const char* password = "";
const char* mqtt_server = "broker.emqx.io";
const int mqtt_port = 1883;
const char* mqtt_topic = "barrilete/astro01/telemetry";

WiFiClient espClient;
PubSubClient client(espClient);

const int PIN_POT_CARDIO     = 36;
const int PIN_POT_OXI_TEMP   = 39;
const int PIN_POT_MASS_GLU   = 34;
const int PIN_POT_SLEEP_MOOD = 35;
const int PIN_POT_RAD_CO2    = 32;
const int PIN_POT_GRAV_EX    = 33;

const int PIN_SW_ZEROG   = 26;
const int PIN_SW_SLEEP   = 25;
const int PIN_BTN_SAFE   = 27;
const int PIN_LED_GREEN  = 14;
const int PIN_LED_YELLOW = 12;
const int PIN_LED_RED    = 13;
const int PIN_BUZZER     = 15;

const float BASE_PULSE = 70.0;
const float BASE_SPO2  = 98.0;
const float BASE_TEMP  = 36.6;
const float BASE_SYS   = 120.0;
const float BASE_DIA   = 80.0;
const float BASE_GLU   = 90.0;
const float BASE_MASS  = 75.0;

bool countermeasureExecuted = false;
unsigned long lastMsg = 0;

float mapFloat(long x, long in_min, long in_max, float out_min, float out_max) {
  return (float)(x - in_min) * (out_max - out_min) / (float)(in_max - in_min) + out_min;
}

float pctDiff(float val, float base) {
  return fabs(val - base) / base * 100.0;
}

int evalLv(float p, float t1, float t2) {
  if (p > t2) return 2;
  if (p > t1) return 1;
  return 0;
}

void setupWiFi() {
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
  }
}

void reconnectMQTT() {
  if (!client.connected()) {
    String clientId = "BarrileteCosmico-ESP32-" + String(random(0xffff), HEX);
    client.connect(clientId.c_str());
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_SW_ZEROG, INPUT_PULLUP);
  pinMode(PIN_SW_SLEEP, INPUT_PULLUP);
  pinMode(PIN_BTN_SAFE, INPUT_PULLUP);
  pinMode(PIN_LED_GREEN, OUTPUT);
  pinMode(PIN_LED_YELLOW, OUTPUT);
  pinMode(PIN_LED_RED, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);

  setupWiFi();
  client.setServer(mqtt_server, mqtt_port);
  client.setBufferSize(1024);
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    if (!client.connected()) reconnectMQTT();
    client.loop();
  }

  if (digitalRead(PIN_BTN_SAFE) == LOW) {
    countermeasureExecuted = !countermeasureExecuted;
    tone(PIN_BUZZER, 1200, 150);
    delay(250);
  }

  unsigned long now = millis();
  if (now - lastMsg > 1000) {
    lastMsg = now;

    int rCardio = analogRead(PIN_POT_CARDIO);
    int rOxi    = analogRead(PIN_POT_OXI_TEMP);
    int rMass   = analogRead(PIN_POT_MASS_GLU);
    int rSleep  = analogRead(PIN_POT_SLEEP_MOOD);
    int rRad    = analogRead(PIN_POT_RAD_CO2);
    int rGrav   = analogRead(PIN_POT_GRAV_EX);

    bool forceZeroG = (digitalRead(PIN_SW_ZEROG) == HIGH);
    bool isSleeping = (digitalRead(PIN_SW_SLEEP) == HIGH);

    int pulse      = round(mapFloat(rCardio, 0, 4095, 45, 135));
    int sys        = round(mapFloat(rCardio, 0, 4095, 95, 165));
    int dia        = round(mapFloat(rCardio, 0, 4095, 60, 110));
    float spo2     = mapFloat(rOxi, 0, 4095, 88.0, 100.0);
    float temp     = mapFloat(rOxi, 0, 4095, 35.5, 38.5);
    float massKg   = mapFloat(rMass, 0, 4095, 64.0, 78.0);
    int glu        = round(mapFloat(rMass, 0, 4095, 60, 160));
    float sleepH   = mapFloat(rSleep, 0, 4095, 2.0, 10.0);
    int mood       = round(mapFloat(rSleep, 0, 4095, 1, 10));
    int rad        = round(mapFloat(rRad, 0, 4095, 0, 100));
    int co2Ppm     = round(mapFloat(rRad, 0, 4095, 400, 6000));
    float gravityG = forceZeroG ? 0.00 : mapFloat(rGrav, 0, 4095, 0.00, 1.00);
    int ex         = round(mapFloat(rGrav, 0, 4095, 0, 90));

    if (isSleeping) {
      pulse = max(42, pulse - 12);
    }
    int fluidShiftIndex = (gravityG < 0.1) ? min(100, (int)(70 + (sys - 110) * 0.6)) : (int)((1.0 - gravityG) * 40);
    float massLossPct = max(0.0f, (BASE_MASS - massKg) / BASE_MASS * 100.0f);

    int lPulse = evalLv(pctDiff(pulse, BASE_PULSE), 10, 20);
    int lSpo2  = evalLv(max(0.0f, (BASE_SPO2 - spo2) / BASE_SPO2 * 100.0f), 3, 6);
    int lTemp  = evalLv(pctDiff(temp, BASE_TEMP), 1, 2);
    int lBp    = max(evalLv(pctDiff(sys, BASE_SYS), 8, 16), evalLv(pctDiff(dia, BASE_DIA), 10, 20));
    int lGlu   = evalLv(pctDiff(glu, BASE_GLU), 15, 30);
    int lSleep = (sleepH < 4.0) ? 2 : (sleepH <= 6.0 ? 1 : 0);
    int lMood  = (mood <= 3) ? 2 : (mood <= 5 ? 1 : 0);
    int lEx    = (ex < 10) ? 2 : (ex < 30 ? 1 : 0);
    int lRad   = (rad >= 85) ? 2 : (rad >= 60 ? 1 : 0);
    int lMass  = (massLossPct > 8.0) ? 2 : (massLossPct > 4.0 ? 1 : 0);
    int lCo2   = (co2Ppm >= 4000) ? 2 : (co2Ppm >= 2500 ? 1 : 0);

    int levels[11] = {lPulse, lSpo2, lTemp, lBp, lGlu, lSleep, lMood, lEx, lRad, lMass, lCo2};
    int worst = 0, yellowCount = 0;
    for (int i = 0; i < 11; i++) {
      if (levels[i] > worst) worst = levels[i];
      if (levels[i] == 1) yellowCount++;
    }
    bool comboYellow = false;
    if (worst == 1 && yellowCount >= 3) {
      worst = 2;
      comboYellow = true;
    }

    digitalWrite(PIN_LED_GREEN,  worst == 0 ? HIGH : LOW);
    digitalWrite(PIN_LED_YELLOW, worst == 1 ? HIGH : LOW);
    digitalWrite(PIN_LED_RED,    worst == 2 ? HIGH : LOW);

    if (worst == 2 && !countermeasureExecuted) {
      tone(PIN_BUZZER, 880, 300);
    } else {
      noTone(PIN_BUZZER);
    }

    const char* statusTxt = (worst == 2) ? "ROJO" : (worst == 1 ? "AMARILLO" : "VERDE");
    const char* gravLbl = (gravityG < 0.05) ? "MICROGRAVEDAD (0G)" : (gravityG < 0.25 ? "LUNA (0.16G)" : (gravityG < 0.55 ? "MARTE (0.38G)" : "TIERRA / CENTRIFUGA (1G)"));
    const char* actionTxt = (worst == 2)
      ? "ALERTA CRITICA: Ejecutar contramedida (Refugio/LBNP/O2) y enviar reporte al cirujano."
      : (worst == 1 ? "ATENCION: Revisar indicadores en amarillo, hidratar y ajustar rutina ARED." : "Rango nominal. Mantener rutina diaria.");

    char payload[950];
    snprintf(payload, sizeof(payload),
      "{\"missionId\":\"ASTRO-01\",\"team\":\"BARRILETE_COSMICO\",\"timestampMs\":%lu,"
      "\"environment\":{\"zeroGravityActive\":%s,\"gravityG\":%.2f,\"gravityLabel\":\"%s\",\"co2Ppm\":%d},"
      "\"circadian\":{\"isSleeping\":%s,\"sleepState\":\"%s\",\"sleep\":%.1f,\"mood\":%d},"
      "\"biometrics\":{\"pulse\":%d,\"spo2\":%.1f,\"temp\":%.1f,\"sys\":%d,\"dia\":%d,\"glu\":%d,"
      "\"massKg\":%.1f,\"baseMassKg\":%.1f,\"massLossPct\":%.1f,\"fluidShiftIndex\":%d,\"ex\":%d,\"rad\":%d},"
      "\"safety\":{\"alertLevel\":%d,\"statusText\":\"%s\",\"comboYellowAlert\":%s,\"countermeasureActive\":%s,\"recommendedAction\":\"%s\"}}",
      now,
      (gravityG < 0.05) ? "true" : "false", gravityG, gravLbl, co2Ppm,
      isSleeping ? "true" : "false", isSleeping ? "DURMIENDO (ACTIGRAFIA)" : "DESPIERTO (ACTIVIDAD)", sleepH, mood,
      pulse, spo2, temp, sys, dia, glu,
      massKg, BASE_MASS, massLossPct, fluidShiftIndex, ex, rad,
      worst, statusTxt, comboYellow ? "true" : "false", countermeasureExecuted ? "true" : "false", actionTxt
    );

    Serial.println(payload);
    if (client.connected()) {
      client.publish(mqtt_topic, payload);
    }
  }
}