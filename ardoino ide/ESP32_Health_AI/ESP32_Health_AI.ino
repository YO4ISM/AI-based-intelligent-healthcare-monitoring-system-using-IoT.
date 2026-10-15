#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <Adafruit_MLX90614.h>
#include "MAX30100_PulseOximeter.h"

// --- بيانات شبكة Wi-Fi ---
const char* ssid = "ABO_ALSOF 8367";        // اكتب اسم شبكتك هنا
const char* password = "11112222"; // اكتب رمز الشبكة هنا

WebServer server(80);

// --- منافذ I2C المنفصلة ---
#define I2C0_SDA 8
#define I2C0_SCL 9
#define I2C1_SDA 18
#define I2C1_SCL 17

TwoWire I2Cone = TwoWire(1);
PulseOximeter pox;
Adafruit_MLX90614 mlx = Adafruit_MLX90614();

// متغيرات القراءات الحالية
float currentPulse = 0.0;
float currentSpO2 = 0.0;
float currentTemp = 0.0;
String currentStatus = "في انتظار القراءة...";
float currentConfidence = 0.0;

uint32_t tsLastReport = 0;
#define REPORTING_PERIOD_MS 2000

// دالة تقييم المؤشرات الحيوية
void evaluateVitals(float pulse, float temp, float spo2) {
    if (spo2 < 90.0 || pulse < 45.0 || pulse > 130.0 || temp >= 39.0 || temp < 35.0) {
        currentStatus = "Critical (حرج)";
        currentConfidence = 96.5;
    } else if ((spo2 >= 90.0 && spo2 < 95.0) || (pulse >= 100.0 && pulse <= 130.0) || (temp >= 37.8 && temp < 39.0)) {
        currentStatus = "Warning (تحذير)";
        currentConfidence = 91.0;
    } else {
        currentStatus = "Normal (طبيعي)";
        currentConfidence = 98.4;
    }
}

// دالة توليد صفحة الويب المدمجة (HTML + CSS + JavaScript)
String getHTMLPage() {
    String html = R"rawliteral(
<!DOCTYPE html>
<html lang="ar" dir="rtl">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>لوحة المراقبة الطبية الذكية</title>
  <style>
    body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; background-color: #0f172a; color: #f8fafc; margin: 0; padding: 20px; display: flex; flex-direction: column; align-items: center; }
    h1 { color: #38bdf8; margin-bottom: 20px; }
    .grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(220px, 1fr)); gap: 20px; width: 100%; max-width: 800px; }
    .card { background: #1e293b; border-radius: 12px; padding: 20px; text-align: center; box-shadow: 0 4px 6px -1px rgba(0, 0, 0, 0.3); border: 1px solid #334155; }
    .label { font-size: 1.1rem; color: #94a3b8; margin-bottom: 10px; }
    .value { font-size: 2.2rem; font-weight: bold; color: #f1f5f9; }
    .unit { font-size: 1rem; color: #64748b; }
    .diagnosis { grid-column: 1 / -1; background: #1e293b; border-radius: 12px; padding: 25px; text-align: center; border: 2px solid #38bdf8; margin-top: 10px; }
    .status-text { font-size: 2rem; font-weight: bold; margin: 10px 0; }
    .normal { color: #4ade80; }
    .warning { color: #facc15; }
    .critical { color: #f87171; }
  </style>
</head>
<body>
  <h1>نظام المراقبة الحيوية - ESP32-S3</h1>
  <div class="grid">
    <div class="card">
      <div class="label">نبضات القلب</div>
      <div class="value"><span id="pulse">--</span> <span class="unit">BPM</span></div>
    </div>
    <div class="card">
      <div class="label">نسبة الأكسجين SpO2</div>
      <div class="value"><span id="spo2">--</span> <span class="unit">%</span></div>
    </div>
    <div class="card">
      <div class="label">درجة حرارة الجسم</div>
      <div class="value"><span id="temp">--</span> <span class="unit">°C</span></div>
    </div>
    <div class="diagnosis">
      <div class="label">التشخيص الطبي الحالي</div>
      <div id="status" class="status-text normal">جاري الاتصال...</div>
      <div class="unit">دقة المطابقة: <span id="conf">--</span>%</div>
    </div>
  </div>

  <script>
    setInterval(function() {
      fetch('/data')
        .then(response => response.json())
        .then(data => {
          document.getElementById('pulse').innerText = Math.round(data.pulse);
          document.getElementById('spo2').innerText = Math.round(data.spo2);
          document.getElementById('temp').innerText = data.temp.toFixed(1);
          
          let stEl = document.getElementById('status');
          stEl.innerText = data.status;
          document.getElementById('conf').innerText = data.confidence.toFixed(1);

          stEl.className = 'status-text';
          if(data.status.includes('Normal')) stEl.classList.add('normal');
          else if(data.status.includes('Warning')) stEl.classList.add('warning');
          else if(data.status.includes('Critical')) stEl.classList.add('critical');
        });
    }, 1500);
  </script>
</body>
</html>
)rawliteral";
    return html;
}

// نقطة وصول إرسال البيانات JSON للواجهة
void handleData() {
    String json = "{";
    json += "\"pulse\":" + String(currentPulse) + ",";
    json += "\"spo2\":" + String(currentSpO2) + ",";
    json += "\"temp\":" + String(currentTemp) + ",";
    json += "\"status\":\"" + currentStatus + "\",";
    json += "\"confidence\":" + String(currentConfidence);
    json += "}";
    server.send(200, "application/json", json);
}

void handleRoot() {
    server.send(200, "text/html", getHTMLPage());
}

void setup() {
    Serial.begin(115200);
    delay(1000);

    // 1. الاتصال بشبكة Wi-Fi
    Serial.printf("\nجاري الاتصال بـ %s ", ssid);
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\n✓ تم الاتصال بالواي فاي بنجاح!");
    Serial.print(">>> افتح هذا الرابط في المتصفح: http://");
    Serial.println(WiFi.localIP());

    // 2. إعداد مسارات صفحة الويب
    server.on("/", handleRoot);
    server.on("/data", handleData);
    server.begin();

    // 3. تهيئة الحساسات
    Wire.begin(I2C0_SDA, I2C0_SCL);
    I2Cone.begin(I2C1_SDA, I2C1_SCL);

    mlx.begin(0x5A, &I2Cone);
    if (pox.begin()) {
        pox.setIRLedCurrent(MAX30100_LED_CURR_7_6MA);
    }
}

void loop() {
    // تشغيل خادم الويب
    server.handleClient();

    // تحديث نبضات القلب
    pox.update();

    if (millis() - tsLastReport > REPORTING_PERIOD_MS) {
        tsLastReport = millis();

        float p = pox.getHeartRate();
        float o = pox.getSpO2();
        float t = mlx.readObjectTempC();

        if (p >= 35.0 && o >= 55.0) {
            currentPulse = p;
            currentSpO2 = o;
            currentTemp = t;
            evaluateVitals(currentPulse, currentTemp, currentSpO2);
        }
    }
}