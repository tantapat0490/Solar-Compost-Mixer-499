#include <ESP8266WiFi.h>        //  เรียกไลบรารีสำหรับควบคุมชิป Wi-Fi ของ ESP8266
#include <ESP8266WebServer.h>   //  เรียกไลบรารีเพื่อสร้าง Web Server เพื่อใช้เขียน HTML
#include "DHT.h"                //  เรียกไลบรารีเซนเซอร์วัดอุณหภูมิ/ความชื้น DHT
#include <math.h>               //  เรียกฟังก์ชันคณิตศาสตร์ เผื่อใช้คำนวณ

// ======================== ตั้งค่าขา ========================
const int RELAY_PIN = D7;   // ขาที่สั่งเปิด/ปิดมอเตอร์ (ผ่านรีเลย์)  
const int ACS_PIN   = A0;   // ขาอนาล็อกที่ต่อ ACS712 OUT
const int DHT_PIN   = D5;   // ขา DATA ของ DHT11

// ======================== DHT11 ========================
#define DHTTYPE DHT11
DHT dht(DHT_PIN, DHTTYPE);  // สร้างตัวแทนเซนเซอร์ DHT

float lastTemp = NAN;       // เก็บค่า Temp ล่าสุด กันค่าหายหรืออ่านผิดพลาด
float lastHumi = NAN;       // เก็บค่า Humi ล่าสุด กันค่าหายหรืออ่านผิดพลาด

// ======================== ค่า ACS712 ========================
// Overcurrent Protect 
const float CURRENT_LIMIT_A    = 26;      // ถ้ากระแสเกิน 26A ระบบจะตัดการทำงานทันที (ปรับค่าได้)
bool faultOverCurrent          = false;   // เป็นตัวแปรสถานะจำว่าตอนนี้ระบบ ติด Fault หรือเปล่า
unsigned long lastProtectCheck = 0;

// แรงดันอ้างอิง ADC บนบอร์ด
const float ACS_VREF  = 5;          // แรงดันอ้างอิง          
const float ACS_SENS  = 0.066;      // ใส่ค่าตามรุ่น
float acsOffsetAdc    = 512.0;      // ค่าเริ่มคร่าวๆ 1023 / 2 = 511.5

// ======================== Wi-Fi ========================
// โหมด STA ให้ต่อ wifi ในวงเดียวกัน (เผื่ออนาคตยังไม่ได้กำหนด)
const char* STA_SSID = "MyPhoneHotspot";   // name wifi 
const char* STA_PASS = "12345678";         // pass wifi 

// โหมด AP สำรอง เวลา Hotspot ไม่เปิด/ต่อไม่ติด
const char* AP_SSID  = "My_AP";
const char* AP_PASS  = "KU80E76AE51";

ESP8266WebServer server(80);              // เปิดพอร์ต 80 HTTP มาตรฐาน รอรับคำสั่ง
String currentMode = "NONE";              // "STA" หรือ "AP"

// ======================== ฟังก์ชันช่วย ========================
String relayState() {
  // เช็คสถานะขารีเลย์ ส่งกลับเป็นข้อความ "ON" หรือ "OFF"
  return (digitalRead(RELAY_PIN) == LOW) ? "ON" : "OFF";  // LOW = ON (Active LOW)
}

// --- ฟังก์ชันหาจุดศูนย์ (Set Zero) ---
void calibrateAcsOffset() {
  const int N = 500;          // กำหนดให้อ่านค่า 500 รอบเพื่อหาค่าเฉลี่ยที่นิ่งที่สุด
  long sum    = 0;            // ตัวแปรสำหรับรวมค่า ADC
  Serial.println("[ACS] Calibrating offset, กรุณาอย่าให้มีกระแสโหลด...");

  for (int i = 0; i < N; i++) {
    sum += analogRead(ACS_PIN);         // สะสมค่าแรงดัน
    delay(2);
  }

  acsOffsetAdc = (float)sum / (float)N; // หาค่าเฉลี่ยเพื่อใช้เป็นค่าอ้างอิงศูนย์
  Serial.print("[ACS] Offset ADC = ");
  Serial.println(acsOffsetAdc);
}

// --- อ่านกระแสจาก ACS712 แบบเบื้องต้น --- 
float readCurrentA() {
  const int N = 100;               // อ่านค่า Analog 100 ครั้งหาค่าเฉลี่ย เพื่อลด Noise
  long sum    = 0;

  for (int i = 0; i < N; i++) {
    sum += analogRead(ACS_PIN);   // อ่านค่า Analog-to-Digital ล่าสุด
    delay(1);
  }

  float adcAvg = (float)sum / (float)N;                // หาค่าเฉลี่ย Analog-to-Digital ค่าที่เราอ่านได้จริงๆ
  float diff   = adcAvg - acsOffsetAdc;                // หาค่าความต่างจากจุดศูนย์
  float vDiff  = diff * (ACS_VREF / 1023);             // แปลงจากเลข Analog-to-Digital (0-1023) เป็นหน่วยแรงดัน Volt
  float amps   = vDiff / ACS_SENS;                     // แปลงหน่วยแรงดันเป็นกระแสแอมป์ (ตาม Sensitivity ของเซนเซอร์)

  // debug ดูใน Serial
  Serial.print("[ACS] adcAvg="); Serial.print(adcAvg);
  Serial.print("  offset=");    Serial.print(acsOffsetAdc);
  Serial.print("  diff=");      Serial.print(diff);
  Serial.print("  amps=");      Serial.println(amps);

  // ถ้าค่าน้อยมาก ให้ถือว่า = 0 (ตัด noise)
  if (amps < 0.2 && amps > -0.2) {                    // ตัด Noise เล็กๆ ทิ้งเพื่อให้หน้าจอนิ่งที่ 0.00A
    amps = 0.0;                                     
  }

  // ไม่ต้องแปลงให้เป็นบวกเสมอ จะได้เห็นทิศทางด้วย
  return amps;                                        // ส่งค่ากระแสที่คำนวณได้กลับไปใช้งานต่อ
}

// อ่าน DHT11 แล้วเก็บค่าไว้ใน lastTemp / lastHumi
void readDhtSafe() {
  float t = dht.readTemperature(); // °C
  float h = dht.readHumidity();    // %RH

  if (!isnan(t) && !isnan(h)) {
    lastTemp = t;
    lastHumi = h;
    Serial.print("[DHT] T="); Serial.print(t);
    Serial.print("  H=");     Serial.println(h);
  } else {
    Serial.println("[DHT] Read failed");
  }
}

void connectWiFiSmart() {
  Serial.println("\n[WiFi] ลองเชื่อมต่อโหมด STA (Hotspot มือถือ)...");
  WiFi.mode(WIFI_STA);                     // สั่งให้ชิป WiFi ทำหน้าที่เป็น Station (STA) ผู้รับสัญญาณ
  WiFi.begin(STA_SSID, STA_PASS);          // ส่งตัวแปรชื่อ WiFi กับรหัสผ่านที่กำหนดไว้ไปให้ระบบ

  unsigned long start      = millis();     // สร้างตัวแปรเก็บ เวลา (ms) ปัจจุบัน นับตั้งแต่เริ่มเปิดบอร์ด
  unsigned long timeout_ms = 8000;         // ตั้งค่า Timeout ไว้ที่ 8,000 ms หรือ 8 s

  // สั่งให้บอร์ด วนลูปอยู่ตรงนี้ ตราบเท่าที่ ยังต่อ WiFi ไม่ติด และ เวลาก็ยังไม่เกิน 8 วินาทีที่เราตั้งไว้
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeout_ms) {
    Serial.print(".");                     // พิมพ์จุด . ออกหน้าจอทุกๆ 0.4 วินาที เพื่อให้เรารู้ว่าบอร์ดไม่ได้ค้าง
    delay(400);
  }

  // เงิ่อนไข เช็คว่า ต่อติดแล้วใช่ไหม
  if (WiFi.status() == WL_CONNECTED) {    // ถ้าติด ให้จำไว้ในตัวแปรว่าตอนนี้เราอยู่ในโหมด STA
    currentMode = "STA";                  // ให้จำไว้ในตัวแปรว่าตอนนี้เราอยู่ในโหมด STA เพื่อเอาไปใช้ในฟังก์ชั่นอื่น

    // พิมพ์ข้อความบอกว่าสำเร็จแล้ว และโชว์หมายเลข IP Address ที่บอร์ดได้รับมา
    Serial.println("\n✅ ต่อ Wi-Fi มือถือ (STA) สำเร็จ");
    Serial.print("IP: ");             
    Serial.println(WiFi.localIP());      
  } 
  else {                                  // ถ้าเป็นอื่นนอกจากนี้ ให้ทำข้างล่างนี้แทน
    Serial.println("\n❌ ต่อ STA ไม่ติด → เปิด AP Mode แทน");
    WiFi.mode(WIFI_AP);                   // สั่งสลับโหมด จากผู้รับ STA ให้กลายเป็น Access Point (AP) หรือ ผู้ปล่อยสัญญาณแทน
    WiFi.softAP(AP_SSID, AP_PASS);        // สั่งให้บอร์ดปล่อย WiFi ออกมาเองโดยใช้ ตัวแปรชื่อและรหัสผ่านที่กำหนดใว้
    currentMode = "AP";                   // จำไว้ว่าตอนนี้เราอยู่ในโหมด AP ปล่อย WiFi เองเพื่อเอาไปใช้ในฟังก์ชั่นอื่น

    // พิมพ์บอกสถานะว่าตอนนี้เปิดโหมดสำรองแล้วนะ และโชว์ IP ของบอร์ดเอง
    Serial.print("📡 AP SSID: ");
    Serial.println(AP_SSID);
    Serial.print("IP AP: ");
    Serial.println(WiFi.softAPIP());
  }
}

// ======================== API (สำหรับ JS + Python) ========================
void handleApiStatus() {
  String ipStr = (currentMode == "STA")
    ? WiFi.localIP().toString()
    : WiFi.softAPIP().toString();

  float currentA = readCurrentA();
  readDhtSafe();                        // อ่าน DHT ทุกครั้งที่มีการขอสถานะ

  // เตรียมค่า temp/humi เป็น JSON-friendly
  String tempStr = "null";
  String humiStr = "null";
  if (!isnan(lastTemp)) tempStr = String(lastTemp, 1);
  if (!isnan(lastHumi)) humiStr = String(lastHumi, 0);

  String json = "{";
  json += "\"relay\":\""    + relayState()        + "\",";
  json += "\"currentA\":"   + String(currentA, 2) + ",";                            // กระแส Batt -> Motor
  json += "\"temp\":"       + tempStr             + ",";
  json += "\"humi\":"       + humiStr             + ",";                            
  json += "\"fault\":"      + String(faultOverCurrent ? "true" : "false") + ",";
  json += "\"mode\":\""     + currentMode         + "\",";
  json += "\"ip\":\""       + ipStr               + "\"";
  json += "}";

  server.send(200, "application/json", json);
}

void handleApiOn() {
  if (faultOverCurrent) {
    Serial.println("[Relay] ON blocked: Overcurrent fault"); 
    // ไม่ยอม ON ถ้ายังมี fault
    handleApiStatus();
    return;
  }

  digitalWrite(RELAY_PIN, LOW);  // ON (ถ้า Active LOW)
  Serial.println("[Relay] ON");
  handleApiStatus();
}

void handleApiOff() {
  digitalWrite(RELAY_PIN, HIGH); // OFF
  faultOverCurrent = false;      // เคลียร์ fault เมื่อสั่ง OFF
  Serial.println("[Relay] OFF");
  handleApiStatus();
}

// ======================== หน้าเว็บหลัก (live) ========================
void handleRoot() {
  String html = R"HTML(
<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <title>AE-Project</title>
  <style>
    body { font-family: sans-serif; text-align: center; padding: 20px; }
    h2 { margin-bottom: 10px; }
    .btn {
      padding: 12px 28px;
      margin: 10px;
      font-size: 20px;
      border-radius: 6px;
      border: none;
      cursor: pointer;
    }
    .on  { background: #4CAF50; color: white; }
    .off { background: #f44336; color: white; }
    .card {
      border: 1px solid #ccc;
      padding: 15px;
      margin: 10px auto;
      max-width: 360px;
      border-radius: 8px;
    }
    .status { font-size: 18px; margin: 5px 0; }
  </style>
</head>
<body>
  <h2>AE-Project</h2>

  <div class="card">
    <h3>Motor Status [D7]</h3>
    <p class="status">Relay: <span id="relay">-</span></p>
    <button class="btn on"  onclick="sendCmd('on')">ON</button>
    <button class="btn off" onclick="sendCmd('off')">OFF</button>
  </div>

  <div class="card">
    <h3>กระแส (ACS712) [A0]</h3>
    <p class="status">Current: <span id="current">-</span> A</p>
  </div>

  <div class="card">
    <h3>อุณหภูมิ / ความชื้น (DHT11 [D5])</h3>
    <p class="status">Temp: <span id="temp">-</span> °C</p>
    <p class="status">Humi: <span id="humi">-</span> %RH</p>
  </div>

  <div class="card">
    <h3>Network</h3>
    <p class="status">Mode: <span id="mode">-</span></p>
    <p class="status">IP: <span id="ip">-</span></p>
  </div>

<script>
async function fetchStatus() {
  try {
    let res = await fetch('/api/status');
    let data = await res.json();
    document.getElementById('relay').innerText = data.relay;
    document.getElementById('current').innerText = data.current.toFixed(2);
    document.getElementById('mode').innerText  = data.mode;
    document.getElementById('ip').innerText    = data.ip;

    if (data.temp !== null) {
      document.getElementById('temp').innerText = data.temp.toFixed(1);
    } else {
      document.getElementById('temp').innerText = "-";
    }

    if (data.humi !== null) {
      document.getElementById('humi').innerText = data.humi.toFixed(0);
    } else {
      document.getElementById('humi').innerText = "-";
    }

  } catch(e) {
    console.log(e);
  }
}

async function sendCmd(cmd) {
  try {
    await fetch('/api/' + cmd);
    fetchStatus(); // อัปเดตสถานะทันทีหลังสั่ง
  } catch(e) {
    console.log(e);
  }
}

// ดึงสถานะทุก 3 วินาที (live)
setInterval(fetchStatus, 3000);
fetchStatus();
</script>
</body>
</html>
)HTML";

  server.send(200, "text/html", html);
}

// ======================== SETUP / LOOP ========================
void setup() {
  Serial.begin(115200);
  delay(2000);

  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH);  // เริ่มต้นปิดไว้ก่อน (ถ้า Active LOW)

  pinMode(ACS_PIN, INPUT);

  dht.begin();

  connectWiFiSmart();

  calibrateAcsOffset();          // คาลิเบรต offset ตอน 0A (อย่าให้มีโหลดผ่าน ACS ตอนเปิดบอร์ด)

  server.on("/",          HTTP_GET, handleRoot);
  server.on("/api/status",HTTP_GET, handleApiStatus);
  server.on("/api/on",    HTTP_GET, handleApiOn);
  server.on("/api/off",   HTTP_GET, handleApiOff);

  server.begin();
  Serial.println("HTTP server started");
}

void loop() {
  server.handleClient();

  // ------ Overcurrent Protection ------
  unsigned long now = millis();
  if (now - lastProtectCheck >= 500) {     // เช็กทุก 500 ms (ครึ่งวิ)
    lastProtectCheck = now;

    float I = readCurrentA();              // อ่านกระแสล่าสุด
    if (I > CURRENT_LIMIT_A) {
      if (!faultOverCurrent) {
        Serial.print("[PROTECT] Over current! I = ");
        Serial.println(I);
      }
      faultOverCurrent = true;
      digitalWrite(RELAY_PIN, HIGH);       // ปิดมอเตอร์ (Active LOW → HIGH = OFF)
    }
  }
}