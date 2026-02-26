/*
  XIAO ESP32C6 Controller with MPU6050 & DRV8833
  - AP Mode: XIAO-ESP32C6 / 12345678
  - I2C: SDA=GPIO4, SCL=GPIO5
  - Motor: AIN1=10, AIN2=9, SLEEP=8
*/

#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>

// --- 設定 ---
const char* AP_SSID = "XIAO-ESP32C6";
const char* AP_PASS = "12345678";

// ピン定義
const int PIN_AIN1   = 16;
const int PIN_AIN2   = 17;
const int PIN_SDA    = 0;
const int PIN_SCL    = 1;
const int PIN_ANT    = 14; // 内蔵/外部アンテナ切替
const int PIN_LED    = 15; // 接続状態LED

uint8_t mpu6050Addr = 0x68;
unsigned long lastMpuPrintMs = 0;
unsigned long ledBlinkUntilMs = 0;

// PWM設定
const int PWM_FREQ = 20000;
const int PWM_RESOLUTION_BITS = 8;
const int PWM_MAX_DUTY = (1 << PWM_RESOLUTION_BITS) - 1;
const int PWM_STEP = 25;

enum MotorDirection { MOTOR_STOP, MOTOR_FORWARD, MOTOR_REVERSE };
MotorDirection currentDirection = MOTOR_STOP;
int currentSpeedDuty = 0;

WebServer server(80);

// --- I2C / MPU6050 関数 ---

void mpuWriteRegister(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(mpu6050Addr);
    Wire.write(reg);
    Wire.write(value);
    Wire.endTransmission();
}

bool mpuInit() {
    // 応答確認
    Wire.beginTransmission(mpu6050Addr);
    if (Wire.endTransmission() != 0) return false;

    // デバイスリセット
    mpuWriteRegister(0x6B, 0x80); 
    delay(100);
    // スリープ解除
    mpuWriteRegister(0x6B, 0x00); 
    delay(100);
    // 加速度計設定 (±2g)
    mpuWriteRegister(0x1C, 0x00);
    return true;
}

bool mpuReadAccelRaw(int16_t& ax, int16_t& ay, int16_t& az) {
    Wire.beginTransmission(mpu6050Addr);
    Wire.write(0x3B);
    if (Wire.endTransmission(false) != 0) return false;

    if (Wire.requestFrom((int)mpu6050Addr, 6) != 6) return false;

    ax = (int16_t)((Wire.read() << 8) | Wire.read());
    ay = (int16_t)((Wire.read() << 8) | Wire.read());
    az = (int16_t)((Wire.read() << 8) | Wire.read());
    return true;
}

// --- モーター制御関数 ---

void applyMotorOutput() {
    if (currentDirection == MOTOR_FORWARD) {
        ledcWrite(PIN_AIN1, currentSpeedDuty);
        ledcWrite(PIN_AIN2, 0);
    } else if (currentDirection == MOTOR_REVERSE) {
        ledcWrite(PIN_AIN1, 0);
        ledcWrite(PIN_AIN2, currentSpeedDuty);
    } else {
        ledcWrite(PIN_AIN1, 0);
        ledcWrite(PIN_AIN2, 0);
    }
}

// --- Webサーバー ハンドラ ---

const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!doctype html><html><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1.0,user-scalable=no">
<title>XIAO SFC Pad</title><style>
body{background:#f0f2f5;font-family:sans-serif;display:flex;flex-direction:column;align-items:center;padding:20px;}
.row{display:flex;gap:24px;align-items:center;margin:12px 0;}
.dpad{display:grid;grid-template-columns:repeat(3,49px);gap:8px;}
.face{display:grid;grid-template-columns:repeat(3,49px);grid-template-rows:repeat(3,49px);gap:8px;}
button{width:49px;height:49px;border-radius:12px;border:none;background:#333;color:#fff;font-size:18px;touch-action:manipulation;}
button:active{background:#666;}
.btn-a{background:#e53935;}
.btn-b{background:#fbc02d;color:#333;}
.btn-y{background:#43a047;}
.btn-x{background:#1e88e5;}
.status{font-weight:bold;color:#444;}
.accel{margin-top:8px;font-size:14px;color:#333;background:#fff;padding:8px 12px;border-radius:8px;box-shadow:0 2px 6px rgba(0,0,0,.08);min-width:240px;text-align:center;}
</style></head><body>
<h2>XIAO ESP32C6 SFC Pad</h2>
<div class="row">
    <div class="dpad">
        <div></div><button onpointerdown="send('UP')">↑</button><div></div>
        <button onpointerdown="send('LEFT')">←</button>
        <div></div>
        <button onpointerdown="send('RIGHT')">→</button>
        <div></div><button onpointerdown="send('DOWN')">↓</button><div></div>
    </div>
    <div class="face">
        <div></div><button class="btn-x" onpointerdown="send('X')">X</button><div></div>
        <button class="btn-y" onpointerdown="send('Y')">Y</button>
        <div></div>
        <button class="btn-a" onpointerdown="send('A')">A</button>
        <div></div><button class="btn-b" onpointerdown="send('B')">B</button><div></div>
    </div>
</div>
<p class="status" id="st">Ready</p>
<div class="accel" id="acc">AX: -- / AY: -- / AZ: --</div>
<div class="accel" id="pwm">PWM Duty: --</div>
<script>
async function send(b){
    document.getElementById('st').textContent='Send: '+b;
    try{ await fetch('/press?btn='+b); }catch(e){}
}
async function updateAccel(){
    try{
        const res = await fetch('/status');
        const d = await res.json();
        document.getElementById('acc').textContent = `AX: ${d.ax} / AY: ${d.ay} / AZ: ${d.az}`;
        document.getElementById('pwm').textContent = `PWM Duty: ${d.duty}`;
    }catch(e){
        document.getElementById('acc').textContent = 'AX: -- / AY: -- / AZ: --';
        document.getElementById('pwm').textContent = 'PWM Duty: --';
    }
}
setInterval(updateAccel, 200);
updateAccel();
</script></body></html>
)rawliteral";

void handleRoot() { server.send(200, "text/html", INDEX_HTML); }

void handlePress() {
    String btn = server.arg("btn");
    Serial.printf("[BTN] %s\n", btn.c_str());
    // digitalWrite(PIN_SLEEP, HIGH);

    ledBlinkUntilMs = millis() + 150;
    digitalWrite(PIN_LED, LOW);

    if (btn == "UP") {
        currentDirection = MOTOR_FORWARD;
        if (currentSpeedDuty == 0) currentSpeedDuty = PWM_STEP;
    } else if (btn == "DOWN") {
        currentDirection = MOTOR_REVERSE;
        if (currentSpeedDuty == 0) currentSpeedDuty = PWM_STEP;
    } else if (btn == "X" || btn == "STOP") {
        currentSpeedDuty = 0;
        currentDirection = MOTOR_STOP;
    } else if (btn == "A") {
        currentSpeedDuty = min(currentSpeedDuty + PWM_STEP, PWM_MAX_DUTY);
    } else if (btn == "B") {
        currentSpeedDuty = max(currentSpeedDuty - PWM_STEP, 0);
        if (currentSpeedDuty == 0) currentDirection = MOTOR_STOP;
    }
    
    applyMotorOutput();
    server.send(200, "text/plain", "OK");
}

void handleAccel() {
    int16_t ax, ay, az;
    if (mpuReadAccelRaw(ax, ay, az)) {
        String payload = "{\"ax\":" + String(ax) + ",\"ay\":" + String(ay) + ",\"az\":" + String(az) + "}";
        server.send(200, "application/json", payload);
    } else {
        server.send(500, "application/json", "{\"error\":\"read_failed\"}");
    }
}

void handleStatus() {
    int16_t ax, ay, az;
    if (mpuReadAccelRaw(ax, ay, az)) {
        String payload = "{\"ax\":" + String(ax) + ",\"ay\":" + String(ay) + ",\"az\":" + String(az) + ",\"duty\":" + String(currentSpeedDuty) + "}";
        server.send(200, "application/json", payload);
    } else {
        String payload = "{\"ax\":null,\"ay\":null,\"az\":null,\"duty\":" + String(currentSpeedDuty) + "}";
        server.send(200, "application/json", payload);
    }
}

// --- Setup & Loop ---

void setup() {
    Serial.begin(115200);
    delay(500);

    // ピン初期化
    //pinMode(PIN_SLEEP, OUTPUT);
    pinMode(PIN_ANT, OUTPUT);
    pinMode(PIN_LED, OUTPUT);
    //pinMode(PIN_NFAULT, INPUT_PULLUP);
    //digitalWrite(PIN_SLEEP, HIGH);
    digitalWrite(PIN_ANT, LOW); // 内蔵アンテナ
    digitalWrite(PIN_LED, HIGH);

    // PWM設定
    ledcAttach(PIN_AIN1, PWM_FREQ, PWM_RESOLUTION_BITS);
    ledcAttach(PIN_AIN2, PWM_FREQ, PWM_RESOLUTION_BITS);

    // I2C初期化
    Wire.begin(PIN_SDA, PIN_SCL, 100000);
    delay(100);

    Serial.println("\n--- Initializing MPU6050 ---");
    if (mpuInit()) {
        Serial.println("MPU6050: OK");
    } else {
        Serial.println("MPU6050: FAILED (Check wiring/address)");
    }

    // WiFi AP開始
    WiFi.softAP(AP_SSID, AP_PASS);
    server.on("/", handleRoot);
    server.on("/press", handlePress);
    server.on("/accel", handleAccel);
    server.on("/status", handleStatus);
    server.begin();

    Serial.println("System Ready.");
    Serial.print("IP: "); Serial.println(WiFi.softAPIP());
}

void loop() {
    server.handleClient();
    if (ledBlinkUntilMs != 0 && millis() > ledBlinkUntilMs) {
        ledBlinkUntilMs = 0;
        digitalWrite(PIN_LED, HIGH);
    }

    /* 異常検知
    if (digitalRead(PIN_NFAULT) == LOW) {
        currentDirection = MOTOR_STOP;
        applyMotorOutput();
        Serial.println("[ALERT] Motor Driver Fault!");
        delay(1000);
    }
    */

    // 500ms周期でMPUデータ表示
    unsigned long now = millis();
    if (now - lastMpuPrintMs >= 500) {
        lastMpuPrintMs = now;
        int16_t ax, ay, az;
        if (mpuReadAccelRaw(ax, ay, az)) {
            Serial.printf("[MPU] AX:%d AY:%d AZ:%d\n", ax, ay, az);
        } else {
            Serial.println("[MPU] Read Error!");
        }
    }
}
