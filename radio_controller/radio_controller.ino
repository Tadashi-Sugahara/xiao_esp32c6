/*
xiao ESP32C6 でAPモードでWebサーバーを立ち上げる。Webサーバーは、スーパーファミコン風のゲームパッドを表示
して、ゲームパッドのボタンを押すと、シリアルモニタに押されたボタンの名前を表示する。

ESP32C6にモータドライバ（DRV8833)と加速度センサ（MPU-6050)が接続されている。
ANT_SEL: GPIO14 (ESP32C6の内蔵アンテナと外部アンテナの切り替えに使用。LOWで内蔵アンテナ、HIGHで外部アンテナ)

AIN1：GPIO4
AIN2：GPIO5
SLEEP:GPIO2
nFAULT:GPIO1

SDA: GPIO06
SCL: GPIO07
*/

#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>

const char* AP_SSID = "XIAO-ESP32C6";
const char* AP_PASS = "12345678";

const int PIN_AIN1 = 10;
const int PIN_AIN2 = 9;
const int PIN_SLEEP = 8;
const int PIN_NFAULT = 7;

const int PIN_SDA = 4;
const int PIN_SCL = 5;
const int PIN_ANT = 14;

const uint8_t MPU6050_ADDR = 0x68;

unsigned long lastMpuPrintMs = 0;

WebServer server(80);

void motorStop() {
	digitalWrite(PIN_AIN1, LOW);
	digitalWrite(PIN_AIN2, LOW);
}

void motorForward() {
	digitalWrite(PIN_AIN1, HIGH);
	digitalWrite(PIN_AIN2, LOW);
}

void motorReverse() {
	digitalWrite(PIN_AIN1, LOW);
	digitalWrite(PIN_AIN2, HIGH);
}

void mpuWriteRegister(uint8_t reg, uint8_t value) {
	Wire.beginTransmission(MPU6050_ADDR);
	Wire.write(reg);
	Wire.write(value);
	Wire.endTransmission();
}

bool mpuInit() {
	Wire.beginTransmission(MPU6050_ADDR);
	if (Wire.endTransmission() != 0) {
		return false;
	}

	mpuWriteRegister(0x6B, 0x00);
	delay(10);
	mpuWriteRegister(0x1C, 0x00);

	return true;
}

bool mpuReadAccelRaw(int16_t& ax, int16_t& ay, int16_t& az) {
	Wire.beginTransmission(MPU6050_ADDR);
	Wire.write(0x3B);
	if (Wire.endTransmission(false) != 0) {
		return false;
	}

	uint8_t readBytes = Wire.requestFrom((int)MPU6050_ADDR, 6);
	if (readBytes != 6) {
		return false;
	}

	ax = (int16_t)((Wire.read() << 8) | Wire.read());
	ay = (int16_t)((Wire.read() << 8) | Wire.read());
	az = (int16_t)((Wire.read() << 8) | Wire.read());
	return true;
}

const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!doctype html>
<html lang="ja">
<head>
	<meta charset="UTF-8" />
	<meta name="viewport" content="width=device-width,initial-scale=1.0,user-scalable=no" />
	<title>XIAO ESP32C6 GamePad</title>
	<style>
		:root {
			--body: #f2f3f7;
			--pad: #d8dbe1;
			--dark: #3e4450;
			--mid: #6f7888;
			--light: #ffffff;
			--purple: #8a6fd6;
			--magenta: #d45ec7;
			--cyan: #5cb9e6;
			--green: #7fcf7a;
			--shadow: rgba(0,0,0,.18);
		}

		* { box-sizing: border-box; }

		body {
			margin: 0;
			min-height: 100vh;
			font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
			background: radial-gradient(circle at 50% 20%, #ffffff, var(--body));
			display: grid;
			place-items: center;
			padding: 16px;
			color: #1f2530;
		}

		.wrap {
			width: min(760px, 100%);
			background: linear-gradient(180deg, #eceff4, #d9dde4);
			border-radius: 28px;
			padding: 18px;
			box-shadow:
				inset 0 3px 0 #ffffff,
				inset 0 -5px 10px rgba(0,0,0,.12),
				0 10px 24px var(--shadow);
			border: 2px solid #c9ced8;
		}

		h1 {
			margin: 0 0 8px;
			font-size: clamp(16px, 2.8vw, 22px);
			text-align: center;
			letter-spacing: .4px;
		}

		.hint {
			text-align: center;
			margin: 0 0 14px;
			color: #4d5564;
			font-size: 14px;
		}

		.pad {
			display: grid;
			grid-template-columns: 1fr auto 1fr;
			align-items: center;
			gap: 16px;
		}

		.button {
			appearance: none;
			border: none;
			cursor: pointer;
			user-select: none;
			-webkit-tap-highlight-color: transparent;
			touch-action: manipulation;
		}

		.dpad {
			width: 170px;
			height: 170px;
			position: relative;
			margin: 0 auto;
		}

		.d {
			position: absolute;
			width: 58px;
			height: 58px;
			background: linear-gradient(180deg, #596273, #39414f);
			color: #f4f7ff;
			font-weight: 700;
			font-size: 18px;
			border-radius: 12px;
			box-shadow: inset 0 2px 0 rgba(255,255,255,.18), 0 3px 8px rgba(0,0,0,.32);
		}

		.d:active, .face:active, .meta:active {
			transform: translateY(2px) scale(.98);
			filter: brightness(.92);
		}

		.up    { left: 56px; top: 0; }
		.down  { left: 56px; bottom: 0; }
		.left  { left: 0; top: 56px; }
		.right { right: 0; top: 56px; }

		.center-dot {
			position: absolute;
			left: 56px;
			top: 56px;
			width: 58px;
			height: 58px;
			border-radius: 14px;
			background: #4a5260;
			box-shadow: inset 0 1px 0 rgba(255,255,255,.1);
		}

		.middle {
			display: grid;
			gap: 10px;
			justify-items: center;
		}

		.meta-row {
			display: flex;
			gap: 8px;
		}

		.meta {
			min-width: 88px;
			padding: 10px 12px;
			border-radius: 999px;
			background: linear-gradient(180deg, #9ca5b4, #808998);
			color: white;
			font-weight: 700;
			font-size: 13px;
			box-shadow: inset 0 2px 0 rgba(255,255,255,.24), 0 3px 7px rgba(0,0,0,.25);
		}

		.shoulders {
			display: flex;
			gap: 10px;
		}

		.shoulders .meta {
			min-width: 70px;
		}

		.abxy {
			width: 190px;
			height: 170px;
			margin: 0 auto;
			position: relative;
		}

		.face {
			position: absolute;
			width: 54px;
			height: 54px;
			border-radius: 50%;
			color: white;
			font-weight: 800;
			font-size: 18px;
			box-shadow: inset 0 2px 0 rgba(255,255,255,.25), 0 4px 8px rgba(0,0,0,.3);
		}

		.y { left: 36px; top: 58px; background: linear-gradient(180deg, #9a83e0, var(--purple)); }
		.x { left: 90px; top: 4px;  background: linear-gradient(180deg, #7fd0f2, var(--cyan)); }
		.b { left: 90px; top: 112px;background: linear-gradient(180deg, #89da83, var(--green)); }
		.a { left: 144px; top: 58px;background: linear-gradient(180deg, #e47ad7, var(--magenta)); }

		#status {
			margin-top: 14px;
			text-align: center;
			font-weight: 700;
			color: #333b48;
			min-height: 22px;
		}

		@media (max-width: 700px) {
			.pad {
				grid-template-columns: 1fr;
			}
			.middle { order: 3; }
			.abxy { order: 2; }
			.dpad { order: 1; }
		}
	</style>
</head>
<body>
	<main class="wrap">
		<h1>Super Famicom 風 GamePad</h1>
		<p class="hint">ボタンを押すとESP32C6のシリアルモニタに名前を表示します</p>

		<section class="pad">
			<div class="dpad">
				<button class="button d up"    data-btn="UP">↑</button>
				<button class="button d down"  data-btn="DOWN">↓</button>
				<button class="button d left"  data-btn="LEFT">←</button>
				<button class="button d right" data-btn="RIGHT">→</button>
				<div class="center-dot"></div>
			</div>

			<div class="middle">
				<div class="shoulders">
					<button class="button meta" data-btn="L">L</button>
					<button class="button meta" data-btn="R">R</button>
				</div>
				<div class="meta-row">
					<button class="button meta" data-btn="SELECT">SELECT</button>
					<button class="button meta" data-btn="START">START</button>
				</div>
			</div>

			<div class="abxy">
				<button class="button face y" data-btn="Y">Y</button>
				<button class="button face x" data-btn="X">X</button>
				<button class="button face b" data-btn="B">B</button>
				<button class="button face a" data-btn="A">A</button>
			</div>
		</section>

		<p id="status">Ready</p>
	</main>

	<script>
		const statusEl = document.getElementById('status');

		async function sendButton(buttonName) {
			statusEl.textContent = `押下: ${buttonName}`;
			try {
				const res = await fetch(`/press?btn=${encodeURIComponent(buttonName)}`, {
					method: 'GET',
					cache: 'no-store'
				});
				if (!res.ok) {
					statusEl.textContent = `送信失敗: ${buttonName}`;
				}
			} catch (e) {
				statusEl.textContent = `通信エラー: ${buttonName}`;
			}
		}

		document.querySelectorAll('[data-btn]').forEach((btn) => {
			btn.addEventListener('pointerdown', () => sendButton(btn.dataset.btn));
		});

		document.querySelectorAll('.d').forEach((btn) => {
			btn.addEventListener('pointerup', () => sendButton('STOP'));
			btn.addEventListener('pointercancel', () => sendButton('STOP'));
			btn.addEventListener('pointerleave', () => sendButton('STOP'));
		});
	</script>
</body>
</html>
)rawliteral";

void handleRoot() {
	server.send(200, "text/html; charset=UTF-8", INDEX_HTML);
}

void handlePress() {
	if (!server.hasArg("btn")) {
		server.send(400, "text/plain", "Missing btn");
		return;
	}

	String button = server.arg("btn");
	Serial.print("[BUTTON] ");
	Serial.println(button);

	if (button == "UP") {
		digitalWrite(PIN_SLEEP, HIGH);
		motorForward();
	} else if (button == "DOWN") {
		digitalWrite(PIN_SLEEP, HIGH);
		motorReverse();
	} else if (button == "STOP" || button == "LEFT" || button == "RIGHT") {
		motorStop();
	} else if (button == "START") {
		digitalWrite(PIN_SLEEP, HIGH);
		Serial.println("[DRV8833] Wake (SLEEP=HIGH)");
	} else if (button == "SELECT") {
		motorStop();
		digitalWrite(PIN_SLEEP, LOW);
		Serial.println("[DRV8833] Sleep (SLEEP=LOW)");
	}

	server.send(200, "text/plain", "OK");
}

void setup() {
	Serial.begin(115200);
	delay(200);

	pinMode(PIN_AIN1, OUTPUT);
	pinMode(PIN_AIN2, OUTPUT);
	pinMode(PIN_SLEEP, OUTPUT);
  pinMode(PIN_ANT, OUTPUT);
	pinMode(PIN_NFAULT, INPUT_PULLUP);
	motorStop();
  digitalWrite(PIN_ANT, LOW);
	digitalWrite(PIN_SLEEP, HIGH);

	Wire.begin(PIN_SDA, PIN_SCL);
	bool mpuOk = mpuInit();
	Serial.print("MPU-6050 init: ");
	Serial.println(mpuOk ? "OK" : "FAILED");

	WiFi.mode(WIFI_AP);
	bool apOk = WiFi.softAP(AP_SSID, AP_PASS);

	Serial.println();
	Serial.println("=== XIAO ESP32C6 AP Web Controller ===");
	Serial.print("AP start: ");
	Serial.println(apOk ? "OK" : "FAILED");
	Serial.print("SSID: ");
	Serial.println(AP_SSID);
	Serial.print("PASS: ");
	Serial.println(AP_PASS);
	Serial.print("IP: ");
	Serial.println(WiFi.softAPIP());
	Serial.print("DRV8833 nFAULT: ");
	Serial.println(digitalRead(PIN_NFAULT) == LOW ? "FAULT" : "OK");

	server.on("/", HTTP_GET, handleRoot);
	server.on("/press", HTTP_GET, handlePress);
	server.begin();
	Serial.println("HTTP server started");
}

void loop() {
	server.handleClient();

	if (digitalRead(PIN_NFAULT) == LOW) {
		motorStop();
		Serial.println("[DRV8833] FAULT detected (nFAULT=LOW)");
		delay(50);
	}

	unsigned long now = millis();
	if (now - lastMpuPrintMs >= 500) {
		lastMpuPrintMs = now;

		int16_t ax = 0;
		int16_t ay = 0;
		int16_t az = 0;
		if (mpuReadAccelRaw(ax, ay, az)) {
			Serial.print("[MPU6050] ax=");
			Serial.print(ax);
			Serial.print(" ay=");
			Serial.print(ay);
			Serial.print(" az=");
			Serial.println(az);
		} else {
			Serial.println("[MPU6050] read error");
		}
	}
}
