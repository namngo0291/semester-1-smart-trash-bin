#include <ESP8266WiFi.h>
#include <PubSubClient.h>

/* =============== WIFI CONFIG =============== */
const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_PASSWORD_WIFI";

/* =============== THINGSBOARD MQTT CONFIG =============== */
const char* TB_SERVER = "thingsboard.cloud";
const int   TB_PORT   = 1883;
const char* TB_TOKEN  = "YOUR_DEVICE_TOKEN";  // token device của mày

WiFiClient espClient;
PubSubClient client(espClient);

/* =============== UART BUFFER =============== */
// Nhận dữ liệu từ 8051 dạng từng dòng: T=..., BIN=..., FIRE=..., ALARM=...
String uartBuffer = "";

/* =============== WIFI =============== */
void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.println("Connecting WiFi...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.print("\nWiFi OK, IP = ");
  Serial.println(WiFi.localIP());
}

/* =============== GỬI TELEMETRY =============== */
void sendTelemetry(const String &json) {
  client.publish("v1/devices/me/telemetry", json.c_str());
  Serial.print("Sent to TB: ");
  Serial.println(json);
}

/* =============== GỬI LỆNH CHO 8051 QUA TX =============== */
/* Giao thức:
 *   "$O\n" -> servo mở (manual)
 *   "$C\n" -> servo đóng (manual)
 */
void sendCmdTo8051(char cmd) {
  Serial.write('$');
  Serial.write(cmd);     // 'O' hoặc 'C'
  Serial.write('\n');
  Serial.flush();

  Serial.print("Send CMD to 8051: ");
  Serial.println(cmd);
}

/* =============== MQTT CALLBACK (NHẬN RPC) =============== */
// payload kiểu JSON: {"method":"setLedMode","params":1}
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  Serial.print("MQTT topic: ");
  Serial.println(topic);

  String t = String(topic);

  // Chỉ xử lý RPC từ ThingsBoard
  if (!t.startsWith("v1/devices/me/rpc/request/")) {
    return;
  }

  // Chuyển payload -> String để dễ parse
  String json;
  for (unsigned int i = 0; i < length; i++) {
    json += (char)payload[i];
  }
  Serial.print("RPC payload: ");
  Serial.println(json);

  // Kiểm tra method "setLedMode"
  int mPos = json.indexOf("\"method\"");
  if (mPos < 0) return;

  int mColon = json.indexOf(':', mPos);
  if (mColon < 0) return;

  int mQuote1 = json.indexOf('"', mColon + 1);
  if (mQuote1 < 0) return;

  int mQuote2 = json.indexOf('"', mQuote1 + 1);
  if (mQuote2 < 0) return;

  String method = json.substring(mQuote1 + 1, mQuote2);
  // Serial.print("Method = ");
  // Serial.println(method);

  if (method != "setLedMode") {
    return;
  }

  // Tìm "params"
  int pPos = json.indexOf("\"params\"");
  if (pPos < 0) return;

  int pColon = json.indexOf(':', pPos);
  if (pColon < 0) return;

  // Bỏ qua khoảng trắng
  int pValIndex = pColon + 1;
  while (pValIndex < (int)json.length() && (json[pValIndex] == ' ' || json[pValIndex] == '\t')) {
    pValIndex++;
  }

  if (pValIndex >= (int)json.length()) return;

  char pc = json[pValIndex];
  int param = (pc == '1') ? 1 : 0;  // 0 hoặc 1 là đủ

  Serial.print("setLedMode params = ");
  Serial.println(param);

  // param = 1 -> mở nắp (manual) -> "$O\n"
  // param = 0 -> đóng nắp (manual) -> "$C\n"
  if (param == 1) {
    sendCmdTo8051('O');
  } else {
    sendCmdTo8051('C');
  }

  // (Không bắt buộc) – gửi response ngược lại cho TB
  int lastSlash = t.lastIndexOf('/');
  if (lastSlash > 0) {
    String reqId = t.substring(lastSlash + 1);
    String respTopic = "v1/devices/me/rpc/response/" + reqId;
    String respJson  = String("{\"result\":") + param + "}";
    client.publish(respTopic.c_str(), respJson.c_str());
  }
}

/* =============== MQTT (ThingsBoard) =============== */
void connectTB() {
  while (!client.connected()) {
    Serial.print("Connecting to TB... ");
    // clientID tuỳ, username = TOKEN, password = NULL
    if (client.connect("ESP8266_Client", TB_TOKEN, NULL)) {
      Serial.println("connected!");

      // Đăng ký nhận RPC
      client.subscribe("v1/devices/me/rpc/request/+");
      Serial.println("Subscribed: v1/devices/me/rpc/request/+");

    } else {
      Serial.print("failed, rc = ");
      Serial.println(client.state());
      delay(2000);
    }
  }
}

/* ================== SETUP ================== */
void setup() {
  // Serial dùng để TX/RX với 8051 @ 9600
  Serial.begin(9600);
  delay(500);

  connectWiFi();
  client.setServer(TB_SERVER, TB_PORT);
  client.setCallback(mqttCallback);
}

/* ================== LOOP ================== */
void loop() {
  if (!client.connected()) connectTB();
  client.loop();

  // ==== ĐỌC UART TỪ 8051 ====
  while (Serial.available()) {
    char c = Serial.read();

    if (c == '\r' || c == '\n') {
      if (uartBuffer.length() > 0) {
        Serial.print("UART from 8051: ");
        Serial.println(uartBuffer);

        // 1) Nhiệt độ: T=+25.3
        if (uartBuffer.startsWith("T=")) {
          String s = uartBuffer.substring(2); // bỏ "T=" -> còn "+25.3"
          float tempC = s.toFloat();          // Arduino tự parse

          // {"temp":25.3}
          String payload = String("{\"temp\":") + String(tempC, 1) + "}";
          sendTelemetry(payload);
        }
        // 2) Trạng thái thùng: BIN=FULL hoặc BIN=OK
        else if (uartBuffer.startsWith("BIN=")) {
          bool full = uartBuffer.indexOf("FULL") >= 0;
          // {"binFull":true/false}
          String payload = String("{\"binFull\":") + (full ? "true" : "false") + "}";
          sendTelemetry(payload);
        }
        // 3) FIRE=0/1
        else if (uartBuffer.startsWith("FIRE=")) {
          bool fire = uartBuffer.endsWith("1");
          String payload = String("{\"fire\":") + (fire ? "true" : "false") + "}";
          sendTelemetry(payload);
        }
        // 4) ALARM=0/1
        else if (uartBuffer.startsWith("ALARM=")) {
          bool alarm = uartBuffer.endsWith("1");
          String payload = String("{\"alarm\":") + (alarm ? "true" : "false") + "}";
          sendTelemetry(payload);
        }

        // reset buffer sau khi xử lý 1 dòng
        uartBuffer = "";
      }
    } else {
      uartBuffer += c;
    }
  }
}
