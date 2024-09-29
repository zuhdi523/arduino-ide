#include <WiFi.h>
#include <WebServer.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>

// Ganti dengan nama dan password untuk Soft AP
const char *apName = "ESP32_AP";
const char *apPassword = "123456789";

// Ganti dengan kredensial MQTT Anda
const char* mqtt_server = "9f100f561c864c9ab4ff48d9950c4985.s1.eu.hivemq.cloud";
const int mqtt_port = 8883;
const char* mqtt_user = "smartplug";
const char* mqtt_password = "Smartplug24";
String topic = ""; // Topik yang akan dibangun
String kelas = ""; // Variabel untuk menyimpan kelas

WiFiClientSecure wifiClient;
PubSubClient client(wifiClient);

const int relayPin = 27; // Pin untuk relay
const int resetButtonPin = 4; // Ganti dengan pin yang Anda pilih

// Inisialisasi server dan WiFi
WebServer server(80);

// Variabel untuk menyimpan SSID dan Password yang dimasukkan oleh pengguna
String ssid = "", password = "";
bool shouldConnect = false; // Flag untuk memulai koneksi WiFi
bool isAPMode = true;       // Untuk memastikan apakah kita berada di mode AP

// HTML untuk halaman konfigurasi
const char *configPage = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <title>ESP32 WiFi Config</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
</head>
<body>
  <h1>ESP32 WiFi Configuration</h1>
  <form action="/save" method="post">
    <label for="ssid">WiFi SSID:</label><br>
    <input type="text" id="ssid" name="ssid"><br>
    <label for="password">WiFi Password:</label><br>
    <input type="text" id="password" name="password"><br>
    <label for="kelas">Kelas:</label><br>
    <input type="text" id="kelas" name="kelas"><br><br>
    <input type="submit" value="Save">
  </form>
</body>
</html>
)rawliteral";

void setup() {
  // Memulai serial communication
  Serial.begin(115200);
  pinMode(relayPin, OUTPUT); // Set pin relay sebagai OUTPUT
  digitalWrite(relayPin, LOW); // Pastikan relay dimatikan saat startup

  pinMode(resetButtonPin, INPUT_PULLUP); // Konfigurasi pin dengan pull-up internal

  // Setup Soft AP
  setupAP();
}

// Fungsi untuk setup Soft AP
void setupAP() {
  // Memulai Soft AP
  WiFi.softAP(apName, apPassword);
  IPAddress IP = WiFi.softAPIP();
  Serial.print("Soft AP IP address: ");
  Serial.println(IP);

  // Mengatur rute HTTP
  server.on("/", handleRoot);
  server.on("/save", handleSave);

  // Mulai server
  server.begin();
}

// Fungsi untuk menyajikan halaman konfigurasi
void handleRoot() {
  server.send(200, "text/html", configPage);
}

// Fungsi untuk menangani form submission
void handleSave() {
  // Menangkap SSID, Password, dan Kelas dari form
  ssid = server.arg("ssid");
  password = server.arg("password");
  kelas = server.arg("kelas");

  // Validasi input SSID, Password, dan Kelas
  if (ssid.length() == 0 || password.length() == 0 || kelas.length() == 0) {
    server.send(400, "text/html", "<h1>Invalid Input!</h1>");
    return;
  }

  // Mengubah topik berdasarkan kelas
  topic = "smartplug/" + kelas;

  Serial.println("Received SSID: " + ssid);
  Serial.println("Received Password: " + password);
  Serial.println("Received Kelas: " + kelas);
  Serial.println("Using topic: " + topic);

  // Menampilkan halaman konfirmasi
  server.send(200, "text/html", "<h1>Configuration Saved</h1><p>Attempting to connect to WiFi...</p>");
  
  // Set flag untuk mulai koneksi WiFi
  shouldConnect = true;
  isAPMode = false;  // Menandai bahwa kita keluar dari mode AP
}

void loop() {
  server.handleClient();

  // Jika flag shouldConnect true, coba hubungkan ke WiFi
  if (shouldConnect) {
    connectToWiFi();  // Coba koneksi ke WiFi
    shouldConnect = false;  // Reset setelah mencoba koneksi
  }

  // Hanya reboot jika tidak dalam mode AP (misal setelah gagal koneksi WiFi)
  if (!isAPMode && WiFi.status() != WL_CONNECTED) {
    delay(2000);  // Tambahkan jeda sebelum restart
    ESP.restart();
  }

  // hubung ke mqtt
  mqtt();

  // kode button reset
  if (digitalRead(resetButtonPin) == LOW) { // Tombol ditekan
      Serial.println("Reset button pressed. Restarting...");
      ESP.restart(); // Melakukan restart
  }
}

// Fungsi untuk menghubungkan ke WiFi dengan SSID dan Password yang telah dimasukkan
void connectToWiFi() {
  // Memutuskan sambungan dari jaringan WiFi sebelumnya jika ada
  WiFi.disconnect(true);

  // Memulai koneksi ke WiFi dengan SSID dan Password yang diterima
  WiFi.begin(ssid.c_str(), password.c_str());
  
  Serial.println("Connecting to WiFi...");
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) { // Maksimum 20 detik menunggu
    delay(1000);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi Connected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());

    // Mematikan Soft AP
    WiFi.softAPdisconnect(true);
    
    // Mengukur kekuatan sinyal awal
    long rssi = WiFi.RSSI();
    Serial.print("Initial Signal Strength (RSSI): ");
    Serial.print(rssi);
    Serial.println(" dBm");
  } else {
    Serial.println("\nFailed to connect to WiFi.");
    // Kembali ke mode Access Point jika gagal tersambung
    setupAP();
    isAPMode = true;  // Tetap di mode AP
  }
}

// Hubung ke mqtt
void mqtt() {
  if (WiFi.status() == WL_CONNECTED) {
    // Mengizinkan koneksi tanpa sertifikat
    wifiClient.setInsecure();
    
    // Setup MQTT
    client.setServer(mqtt_server, mqtt_port);
    client.setCallback(callback);

    // Inisialisasi koneksi MQTT
    connectMQTT();

    if (!client.connected()) {
      connectMQTT();
    }
    client.loop();
  }
}

// Fungsi untuk terhubung ke MQTT
void connectMQTT() {
  while (!client.connected()) {
    Serial.print("Mencoba menghubungkan ke MQTT...");
    
    if (client.connect("ESP32Client", mqtt_user, mqtt_password)) {
      Serial.println("terhubung");
      client.subscribe(topic.c_str()); // Menggunakan topik yang dinamis
    } else {
      Serial.print("Gagal, rc=");
      Serial.print(client.state());
      Serial.println(" coba lagi dalam 5 detik");
      delay(5000);
    }
  }
}

// Callback untuk pesan yang diterima
void callback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  
  Serial.print("Pesan diterima pada topik: ");
  Serial.println(topic);
  Serial.print("Pesan: ");
  Serial.println(message);
  
  // Menggabungkan topik untuk konfirmasi
  String pub = "konf/" + String(topic);

  // Mengontrol relay berdasarkan pesan
  if (message == "ON") {
    digitalWrite(relayPin, HIGH); // Nyalakan relay
    Serial.println("Relay dinyalakan");
    client.publish(pub.c_str(), "ON"); // Kirim konfirmasi
  } else if (message == "OFF") {
    digitalWrite(relayPin, LOW); // Matikan relay
    Serial.println("Relay dimatikan");
    client.publish(pub.c_str(), "OFF"); // Kirim konfirmasi
  }
}
