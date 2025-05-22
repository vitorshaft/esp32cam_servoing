// app_httpd.cpp - versão minimalista
// - Streaming de vídeo MJPEG em QVGA
// - Detecção facial ativável
// - Controle de intensidade do LED
// - Leitura da posição do rosto detectado

#include "esp_http_server.h"
#include "esp_timer.h"
#include "esp_camera.h"
#include "fb_gfx.h"
#include "esp32-hal-ledc.h"
// #include "index_html.h"

// LED
#define LED_LEDC_CHANNEL 2
#define CONFIG_LED_MAX_INTENSITY 255
int led_duty = 0;
bool isStreaming = false;
httpd_handle_t camera_httpd = NULL;

const char* html_page = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>ESP32-CAM Controle</title>
  <style>
    body {
      font-family: Arial, sans-serif;
      text-align: center;
      background: #1a1a1a;
      color: #eee;
    }
    img {
      margin-top: 10px;
      border: 3px solid #555;
      max-width: 100%;
    }
    .controls {
      margin-top: 20px;
    }
    .slider {
      width: 80%;
    }
  </style>
</head>
<body>
  <h2>ESP32-CAM com Detecção Facial</h2>
  <img src="/stream" id="cam_stream" alt="Camera Stream">

  <div class="controls">
    <button onclick="toggleDetection()">Toggle Detecção</button><br><br>
    
    <label for="led">LED Intensity</label><br>
    <input type="range" min="0" max="255" value="0" id="led" class="slider"
      oninput="setLed(this.value)">
  </div>

  <script>
    let detection = true;

    function toggleDetection() {
      detection = !detection;
      fetch(`/control?var=face_detect&val=${detection ? 1 : 0}`)
        .then(() => console.log("Detecção facial: " + (detection ? "ON" : "OFF")));
    }

    function setLed(val) {
      fetch(`/control?var=led_intensity&val=${val}`)
        .then(() => console.log("LED: " + val));
    }
  </script>
</body>
</html>
)rawliteral";

void enable_led(bool en) {
    int duty = en ? led_duty : 0;
    if (en && isStreaming && (led_duty > CONFIG_LED_MAX_INTENSITY)) {
        duty = CONFIG_LED_MAX_INTENSITY;
    }
    ledcWrite(LED_LEDC_CHANNEL, duty);
}

// Face detection
#include <list>
#include "human_face_detect_msr01.hpp"
#include "human_face_detect_mnp01.hpp"
int face_x = -1, face_y = -1, face_w = -1, face_h = -1;
static int8_t detection_enabled = 1;

static esp_err_t index_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, html_page, strlen(html_page));
}

void draw_face_boxes(fb_data_t *fb, std::list<dl::detect::result_t> *results) {
    for (auto it = results->begin(); it != results->end(); ++it) {
        face_x = (int)it->box[0];
        face_y = (int)it->box[1];
        face_w = (int)it->box[2] - face_x + 1;
        face_h = (int)it->box[3] - face_y + 1;
        fb_gfx_drawFastHLine(fb, face_x, face_y, face_w, 0x00FF00);
        fb_gfx_drawFastVLine(fb, face_x, face_y, face_h, 0x00FF00);
        break; // apenas o primeiro
    }
}

void get_last_face_coords(int *x, int *y, int *w, int *h) {
    *x = face_x; *y = face_y; *w = face_w; *h = face_h;
}

// Stream MJPEG com detecção facial
static esp_err_t stream_handler(httpd_req_t *req) {
    camera_fb_t *fb = NULL;
    esp_err_t res = ESP_OK;
    uint8_t *_jpg_buf = NULL;
    size_t _jpg_buf_len = 0;

    httpd_resp_set_type(req, "multipart/x-mixed-replace;boundary=frame");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    isStreaming = true;
    enable_led(true);

    HumanFaceDetectMSR01 s1(0.1F, 0.5F, 10, 0.2F);
    HumanFaceDetectMNP01 s2(0.5F, 0.3F, 5);

    while (true) {
        fb = esp_camera_fb_get();
        if (!fb) continue;

        if (detection_enabled && fb->width <= 400 && fb->format == PIXFORMAT_RGB565) {
            std::list<dl::detect::result_t> &candidates = s1.infer((uint16_t *)fb->buf, {(int)fb->height, (int)fb->width, 3});
            std::list<dl::detect::result_t> &results = s2.infer((uint16_t *)fb->buf, {(int)fb->height, (int)fb->width, 3}, candidates);
            if (results.size() > 0) {
                //fb_data_t rfb = { fb->width, fb->height, fb->buf, 2, FB_RGB565 };
                fb_data_t rfb;
                rfb.width = fb->width;
                rfb.height = fb->height;
                rfb.data = fb->buf;
                rfb.bytes_per_pixel = 2;      // RGB565 = 2 bytes por pixel
                rfb.format = FB_RGB565;  // ✅ correto
                
                draw_face_boxes(&rfb, &results);
            }
        }

        bool jpeg_ok = frame2jpg(fb, 80, &_jpg_buf, &_jpg_buf_len);
        esp_camera_fb_return(fb);
        if (!jpeg_ok) continue;

        httpd_resp_send_chunk(req, "--frame\r\n", strlen("--frame\r\n"));
        httpd_resp_send_chunk(req, "Content-Type: image/jpeg\r\n\r\n", strlen("Content-Type: image/jpeg\r\n\r\n"));
        httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
        httpd_resp_send_chunk(req, "\r\n", strlen("\r\n"));
        free(_jpg_buf);
    }

    isStreaming = false;
    enable_led(false);
    return res;
}

// Ativa/desativa detecção ou define LED
static esp_err_t cmd_handler(httpd_req_t *req) {
    char buf[100];
    httpd_req_get_url_query_str(req, buf, sizeof(buf));
    char variable[32], value[32];
    httpd_query_key_value(buf, "var", variable, sizeof(variable));
    httpd_query_key_value(buf, "val", value, sizeof(value));
    int val = atoi(value);

    if (!strcmp(variable, "face_detect")) detection_enabled = val;
    else if (!strcmp(variable, "led_intensity")) {
    led_duty = val;
    enable_led(true); // ✅ força a atualização imediata
    }
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

void startCameraServer() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 8;
    httpd_start(&camera_httpd, &config);
    httpd_uri_t index_uri = {
      .uri       = "/",
      .method    = HTTP_GET,
      .handler   = index_handler,
      .user_ctx  = NULL
    };
    httpd_uri_t stream_uri = { "/stream", HTTP_GET, stream_handler, NULL };
    httpd_uri_t cmd_uri = { "/control", HTTP_GET, cmd_handler, NULL };
    // httpd_uri_t stream_uri = { "/stream", HTTP_GET, stream_handler, NULL };
    // httpd_uri_t cmd_uri = { "/control", HTTP_GET, cmd_handler, NULL };

    httpd_register_uri_handler(camera_httpd, &index_uri);
    httpd_register_uri_handler(camera_httpd, &stream_uri);
    httpd_register_uri_handler(camera_httpd, &cmd_uri);
}

void setupLedFlash(int pin) {
    ledcSetup(LED_LEDC_CHANNEL, 5000, 8);
    ledcAttachPin(pin, LED_LEDC_CHANNEL);
}
