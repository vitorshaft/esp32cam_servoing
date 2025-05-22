// !!!ATENCAO!!!

//     VERSAO COMPATIVEL COM O PACOTE 2.0.17 DE PLACAS ESP32 NO ARDUINO IDE.
//     VERSOES ATUAIS PODEM NAO COMPILAR O CODIGO CORRETAMENTE!!

#include "esp_camera.h"
#include <WiFi.h>

// Configuração para AI THINKER
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22
#define LED_GPIO_NUM       4
#define SERVO_GPIO_NUM    13  // <- Novo: pino do servo

// Credenciais WiFi
const char* ssid = "SEU SSID";
const char* password = "sua senha";

// Protótipos
void startCameraServer();
void setupLedFlash(int pin);
extern void get_last_face_coords(int *x, int *y, int *w, int *h);

void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println();

  // Configura servo no canal PWM
  ledcSetup(6, 50, 16); // Canal 6, 50Hz, 16-bit
  ledcAttachPin(SERVO_GPIO_NUM, 6); // Liga pino ao canal

  // Configura a câmera
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.frame_size = FRAMESIZE_QVGA; // <- alterado para QVGA (320x240)
  config.pixel_format = PIXFORMAT_RGB565; // ✅ Isso permite detecção facial e stream MJPEG

  config.grab_mode = CAMERA_GRAB_LATEST;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = 10;
  config.fb_count = 2;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera falhou ao iniciar, erro 0x%x", err);
    return;
  }

  setupLedFlash(LED_GPIO_NUM);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi conectado");

  startCameraServer();

  Serial.print("Camera pronta! Acesse 'http://");
  Serial.print(WiFi.localIP());
  Serial.println("' para conectar");
}

void loop() {
  int x, y, w, h;
  get_last_face_coords(&x, &y, &w, &h);

  if (x >= 0) {
    Serial.printf("Rosto detectado: x=%d, y=%d, w=%d, h=%d\n", x, y, w, h);

    // Mapeia posição X da face (0-320) para ângulo do servo (0°–180°)
    int center_x = x + w / 2;
    int angle = map(center_x, 0, 320, 0, 180);
    angle = constrain(angle, 0, 180);

    // Converte ângulo para duty de 16 bits no PWM 50Hz
    int duty = map(angle, 0, 180, 1638, 8192); // 0.5ms–2.5ms (duty 16-bit em 50Hz)

    ledcWrite(6, duty); // Move o servo
  } else {
    Serial.println("Nenhum rosto detectado.");
  }

  delay(200);
}
