#include <Arduino.h>
#include <WiFi.h>
#include <lvgl.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <TJpg_Decoder.h>
#include <FS.h>

#include "RGBLedController.h"
#include "Config.h"
#include "SpotifyAuth.h"
#include "ConfigPortal.h"

#include <iostream>
#include <iomanip>   // Para setw y setfill
#include <sstream>   // Para ostringstream
#include <string>

//========= Touch Screen =========
// Touchscreen pins
#define XPT2046_IRQ 36   // T_IRQ
#define XPT2046_MOSI 32  // T_DIN
#define XPT2046_MISO 39  // T_OUT
#define XPT2046_CLK 25   // T_CLK
#define XPT2046_CS 33    // T_CS

SPIClass touchscreenSPI = SPIClass(VSPI);
XPT2046_Touchscreen touchscreen(XPT2046_CS, XPT2046_IRQ);

#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 320

// Touchscreen coordinates: (x, y) and pressure (z)
int x, y, z;

// Get the Touchscreen data
void touchscreen_read(lv_indev_t * indev, lv_indev_data_t * data) {
  // Checks if Touchscreen was touched, and prints X, Y and Pressure (Z)
  if(touchscreen.tirqTouched() && touchscreen.touched()) {
    // Get Touchscreen points
    TS_Point p = touchscreen.getPoint();
    // Calibrate Touchscreen points with map function to the correct width and height
    x = map(p.x, 200, 3700, 1, SCREEN_WIDTH);
    y = map(p.y, 240, 3800, 1, SCREEN_HEIGHT);
    z = p.z;

    data->state = LV_INDEV_STATE_PRESSED;

    // Set the coordinates
    data->point.x = x;
    data->point.y = y;

    // Print Touchscreen info about X, Y and Pressure (Z) on the Serial Monitor
    /* Serial.print("X = ");
    Serial.print(x);
    Serial.print(" | Y = ");
    Serial.print(y);
    Serial.print(" | Pressure = ");
    Serial.print(z);
    Serial.println();*/
  }
  else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

//========= LVGL =========

#define DRAW_BUF_SIZE (SCREEN_WIDTH * SCREEN_HEIGHT / 10 * (LV_COLOR_DEPTH / 8))
uint32_t draw_buf[DRAW_BUF_SIZE / 4];

//========= Spotify =========

lv_obj_t * song_title;
lv_obj_t * artist;
lv_obj_t * play_pause_button;

lv_obj_t *progress;
lv_obj_t *duration;

lv_obj_t *progress_bar;

String current_song_id = "";
String current_playing_state = "";
String artworkURL = "";

int32_t progress_ms = 0;
int32_t duration_ms = 0;

String accessToken = "";

RGBLedController ledController;

//========= Configuracion =========

// Boton BOOT de la CYD: mantenerlo apretado al arrancar entra a la configuracion
#define BOOT_BUTTON 0
#define WIFI_TIMEOUT_MS 20000

AppConfig config;
ConfigPortal portal(config);

// Pantalla simple con un titulo, un texto y opcionalmente un QR
// Nota: la fuente de LVGL no tiene tildes, por eso los textos van sin acentos
void showInfoScreen(const char* title, const String& text, const String& qrData = "") {
  lv_obj_t * screen = lv_screen_active();
  lv_obj_clean(screen);
  lv_obj_set_style_bg_color(screen, lv_color_hex(0x383b39), 0);

  int32_t textX = 10;
  if (qrData.length() > 0) {
    lv_obj_t * qr = lv_qrcode_create(screen);
    lv_qrcode_set_size(qr, 130);
    lv_qrcode_set_dark_color(qr, lv_color_black());
    lv_qrcode_set_light_color(qr, lv_color_white());
    lv_qrcode_update(qr, qrData.c_str(), qrData.length());
    lv_obj_set_style_border_color(qr, lv_color_white(), 0);
    lv_obj_set_style_border_width(qr, 5, 0);
    lv_obj_align(qr, LV_ALIGN_LEFT_MID, 10, 0);
    textX = 160;
  }

  lv_obj_t * title_label = lv_label_create(screen);
  lv_label_set_text(title_label, title);
  lv_obj_set_style_text_color(title_label, lv_color_hex(0x1db954), 0);
  lv_obj_set_pos(title_label, textX, 20);

  lv_obj_t * text_label = lv_label_create(screen);
  lv_label_set_long_mode(text_label, LV_LABEL_LONG_WRAP);
  lv_label_set_text(text_label, text.c_str());
  lv_obj_set_width(text_label, SCREEN_HEIGHT - textX - 10);
  lv_obj_set_style_text_color(text_label, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_pos(text_label, textX, 50);

  // Dibujar ya, antes de que empiecen operaciones que bloquean (WiFi, HTTP)
  lv_timer_handler();
}

// Crea la red WiFi propia de la CYD para configurarla desde el celu
void startSetupMode() {
  // QR con el formato estandar de WiFi: la camara del celu ofrece conectarse directo
  String wifiQr = String("WIFI:T:nopass;S:") + ConfigPortal::AP_SSID + ";;";
  showInfoScreen("Configuracion",
    "1. Escanea el QR o conectate a la red WiFi \"" + String(ConfigPortal::AP_SSID) + "\"\n\n"
    "2. Se abre la pagina de configuracion (si no, entra a 192.168.4.1)", wifiQr);

  ledController.setLedRed();
  portal.startAccessPoint();
}

//========= WIFI =========

// Devuelve false si no pudo conectar o si se apreto BOOT para ir a la configuracion
bool connectToWifi(const AppConfig& cfg) {
  Serial.println("Conectando al WiFi...");
  showInfoScreen("Conectando...", "Red: " + cfg.wifiSsid + "\n\nMantene apretado BOOT para entrar a la configuracion");

  ledController.setLedRed();

  // Nombre con el que aparece en el router (en muchos anda http://spotify)
  WiFi.setHostname(ConfigPortal::HOSTNAME);
  WiFi.mode(WIFI_STA);
  WiFi.begin(cfg.wifiSsid.c_str(), cfg.wifiPassword.c_str());

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (digitalRead(BOOT_BUTTON) == LOW) {
      Serial.println("BOOT apretado, entrando a la configuracion");
      return false;
    }
    if (millis() - start > WIFI_TIMEOUT_MS) {
      Serial.println("No se pudo conectar al WiFi");
      return false;
    }
    lv_timer_handler();
    delay(100);
  }

  ledController.setLedGreen();

  // Once connected, print the local IP address
  Serial.println("Conectado al WiFi!");
  Serial.print("Red: ");
  Serial.println(WiFi.SSID());
  Serial.print("Direccion IP: ");
  Serial.println(WiFi.localIP());
  Serial.print("Fuerza de la señal (RSSI): ");
  Serial.println(WiFi.RSSI());

  delay(1000);

  ledController.turnOffLed();
  return true;
}

// If logging is enabled, it will inform the user about what is happening in the library
void log_print(lv_log_level_t level, const char * buf) {
  LV_UNUSED(level);
  Serial.println(buf);
  Serial.flush();
}

#include "List_SPIFFS.h"
#include "Web_Fetch.h"

TFT_eSPI tft = TFT_eSPI(); 

bool tft_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap)
{
  // Stop further decoding as image is running off bottom of screen
  if ( y >= tft.height() ) return 0;

  // This function will clip the image block rendering automatically at the TFT boundaries
  tft.pushImage(x, y, w, h, bitmap);

  // Return 1 to decode next block
  return 1;
}

// Dibuja la tapa del album. Si la descarga falla, artworkURL no cambia y se reintenta en el proximo ciclo
void downloadImage(const char* url) {

  // Archivos locales o anuncios pueden no tener imagen: se borra la anterior
  if (url == nullptr) {
    if (artworkURL.length() > 0) {
      artworkURL = "";
      lv_obj_invalidate(lv_screen_active()); // LVGL redibuja toda la pantalla y tapa la imagen vieja
    }
    return;
  }

  if (artworkURL == url) {
    return;
  }

  if(SPIFFS.exists("/albumArt.jpg") == true) {
    SPIFFS.remove("/albumArt.jpg");
  }

  if (!getFile(url, "/albumArt.jpg")) {
    Serial.println("No se pudo descargar el arte de tapa");
    return;
  }

  artworkURL = url;
  TJpgDec.drawFsJpg(5, 5, "/albumArt.jpg");
}

// Spotify manda las imagenes de mayor a menor (640, 300, 64). Se usa la de 300 si esta
const char* getArtworkUrl(JsonDocument& doc) {
  JsonArray images = doc["item"]["album"]["images"];
  if (images.size() == 0) {
    return nullptr;
  }
  return images[images.size() > 1 ? 1 : 0]["url"];
}

std::string convertirMSaMinutosSegundos(long ms) {
  long totalSegundos = ms / 1000;
  int minutos = totalSegundos / 60;
  int segundos = totalSegundos % 60;

  std::ostringstream oss;
  oss << std::setfill('0') << std::setw(2) << minutos << ":"
      << std::setfill('0') << std::setw(2) << segundos;

  return oss.str();
}

void refreshProgress() {
  if (duration_ms > 0 && progress_ms > duration_ms) {
    progress_ms = duration_ms;
  }

  lv_label_set_text(progress, convertirMSaMinutosSegundos(progress_ms).c_str());

  int32_t porcentage = duration_ms > 0 ? (progress_ms * 100) / duration_ms : 0;
  lv_bar_set_value(progress_bar, porcentage, LV_ANIM_ON);
}

void updateSongInfo(JsonDocument& doc) {
  // "item" viene en null con anuncios y podcasts, y con archivos locales faltan campos
  JsonVariant item = doc["item"];
  const char* default_name = doc["currently_playing_type"] == "ad" ? "Anuncio" : "Desconocida";
  const char* track_name = item["name"] | default_name;
  const char* artist_name = item["artists"][0]["name"] | "";

  Serial.println("Canción actual:");
  Serial.println("Nombre: " + String(track_name));
  Serial.println("Artista: " + String(artist_name));
  lv_label_set_text(song_title, track_name);
  lv_label_set_text(artist, artist_name);

  duration_ms = item["duration_ms"] | 0;
  lv_label_set_text(duration, convertirMSaMinutosSegundos(duration_ms).c_str());
  refreshProgress();
}

void updatePlayPauseButton() {
  lv_obj_clean(play_pause_button);
  lv_obj_t * btn_label = lv_label_create(play_pause_button);

  // LVGL ya trae parte de los simbolos de FontAwesome
  if (current_playing_state == "true") {
    lv_label_set_text(btn_label, LV_SYMBOL_PAUSE);
  } else {
    lv_label_set_text(btn_label, LV_SYMBOL_PLAY);
  }
  lv_obj_set_style_text_color(btn_label, lv_color_hex(0x000000), 0);
  lv_obj_center(btn_label);
}

static void updateProgressBar(lv_timer_t *timer) {
  if (duration_ms == 0 || current_playing_state != "true")
    return;

  progress_ms += 1000;
  refreshProgress();
}

static void updateScreen(lv_timer_t *timer) {
  HTTPClient http;
  http.begin("https://api.spotify.com/v1/me/player/currently-playing");
  http.addHeader("Authorization", "Bearer " + accessToken);

  int httpCode = http.GET();

  if (httpCode == 200) {
    String response = http.getString();

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, response);

    if (error) {
      Serial.println("Error al parsear el JSON: " + String(error.c_str()));
      http.end();
      return;
    }

    progress_ms = doc["progress_ms"] | 0;

    // Con archivos locales el id viene en null, la uri siempre esta
    String song_id = doc["item"]["uri"] | "";
    String playing_state = doc["is_playing"].as<bool>() ? "true" : "false";

    bool songChanged = song_id != current_song_id;
    bool stateChanged = playing_state != current_playing_state;
    current_song_id = song_id;
    current_playing_state = playing_state;

    if (songChanged) {
      Serial.println("La cancion cambio");
      updateSongInfo(doc);
    } else {
      refreshProgress();
    }

    if (stateChanged) {
      Serial.println("El estado cambio");
      updatePlayPauseButton();
    }

    // Se llama siempre para reintentar si la descarga anterior fallo (no descarga dos veces la misma)
    downloadImage(getArtworkUrl(doc));
  } else if (httpCode == 401) {
    // El access token dura 1 hora. Se pide uno nuevo y el proximo ciclo del timer reintenta
    Serial.println("Access token vencido");
    SpotifyAuth::refreshAccessToken(config, accessToken);
  } else if (httpCode == 204) {
    Serial.println("No hay reproducción activa en este momento.");
  } else {
    Serial.println("Error al actualizar la cancion, Código HTTP: " + String(httpCode));
    Serial.println("Respuesta: " + http.getString());
  }

  http.end();
}

void playAndPause() {
  HTTPClient http;

  if (current_playing_state == "true") {
    http.begin("https://api.spotify.com/v1/me/player/pause");  // URL para siguiente canción
  } else {
    http.begin("https://api.spotify.com/v1/me/player/play");  // URL para siguiente canción
  }

  http.addHeader("Authorization", "Bearer " + accessToken);  // Cabecera con el token de acceso
  http.addHeader("Content-Length", "0"); // Agregado a la cabecera para que spotify acepte la solicitud

  int httpCode = http.PUT("");  // Enviamos la petición POST (vacía)

  if (httpCode == 200) {
    String payload = http.getString();  // Obtener la respuesta del servidor
    Serial.println("Pausado o reanudado exitoso");
    Serial.println(payload);  // Imprime la respuesta completa
  } else {
    Serial.println("Error al enviar la solicitud");
    Serial.println(httpCode);  // Imprime el código de error HTTP
  }

  http.end();
}

void nextSong() {
  HTTPClient http;
  http.begin("https://api.spotify.com/v1/me/player/next");  // URL para siguiente canción
  http.addHeader("Authorization", "Bearer " + accessToken);  // Cabecera con el token de acceso
  http.addHeader("Content-Length", "0"); // Agregado a la cabecera para que spotify acepte la solicitud
  
  int httpCode = http.POST("");  // Enviamos la petición POST (vacía)

  if (httpCode > 0) {
    String payload = http.getString();  // Obtener la respuesta del servidor
    Serial.println("Siguiente canción enviada");
    Serial.println(payload);  // Imprime la respuesta completa
  } else {
    Serial.println("Error al enviar la solicitud");
    Serial.println(httpCode);  // Imprime el código de error HTTP
  }

  http.end();
}

void prevSong() {
  HTTPClient http;
  http.begin("https://api.spotify.com/v1/me/player/previous");  // URL para siguiente canción
  http.addHeader("Authorization", "Bearer " + accessToken);  // Cabecera con el token de acceso
  http.addHeader("Content-Length", "0"); // Agregado a la cabecera para que spotify acepte la solicitud
  
  int httpCode = http.POST("");  // Enviamos la petición POST (vacía)

  if (httpCode > 0) {
    String payload = http.getString();  // Obtener la respuesta del servidor
    Serial.println("Siguiente canción enviada");
    Serial.println(payload);  // Imprime la respuesta completa
  } else {
    Serial.println("Error al enviar la solicitud");
    Serial.println(httpCode);  // Imprime el código de error HTTP
  }

  http.end();
}

static void event_handler_prev_button(lv_event_t * e) {
  lv_event_code_t code = lv_event_get_code(e);
  if(code == LV_EVENT_CLICKED) {
    LV_LOG_USER("Previous button pressed");
    prevSong();
    updateScreen(NULL);
  }
}

static void event_handler_play_pause_button(lv_event_t * e) {
  lv_event_code_t code = lv_event_get_code(e);
  if(code == LV_EVENT_CLICKED) {
    LV_LOG_USER("Play-Pause button pressed");
    playAndPause();
    updateScreen(NULL);
  }
}

static void event_handler_next_button(lv_event_t * e) {
  lv_event_code_t code = lv_event_get_code(e);
  if(code == LV_EVENT_CLICKED) {
    LV_LOG_USER("Next button pressed");
    nextSong();
    updateScreen(NULL);
  }
}

void screenSetUp() {
  // Start LVGL
  lv_init();
  // LVGL mide el tiempo con millis(), asi no se atrasa cuando una peticion HTTP bloquea el loop
  lv_tick_set_cb([]() -> uint32_t { return millis(); });
  // Register print function for debugging
  lv_log_register_print_cb(log_print);

  // Start the SPI for the touchscreen and init the touchscreen
  touchscreenSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  touchscreen.begin(touchscreenSPI);
  // Set the Touchscreen rotation in landscape mode
  // Note: in some displays, the touchscreen might be upside down, so you might need to set the rotation to 0: touchscreen.setRotation(0);
  touchscreen.setRotation(2);

  // Create a display object
  lv_display_t * disp;
  // Initialize the TFT display using the TFT_eSPI library
  disp = lv_tft_espi_create(SCREEN_WIDTH, SCREEN_HEIGHT, draw_buf, sizeof(draw_buf));
  lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_90);
    
  // Initialize an LVGL input device object (Touchscreen)
  lv_indev_t * indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  // Set the callback function to read Touchscreen input
  lv_indev_set_read_cb(indev, touchscreen_read);
}

void drawMainGui(void) {
  lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(0x383b39), 0);

  song_title = lv_label_create(lv_screen_active());
  lv_label_set_long_mode(song_title, LV_LABEL_LONG_SCROLL_CIRCULAR); // si el nombre es mas largo que el ancho de la pantalla va rotando el string para que se vea completo
  lv_label_set_text(song_title, "Cancion: Desconocida");
  lv_obj_set_width(song_title, SCREEN_HEIGHT-10); //El ancho que puede ocupar el texto es igual al alto de la pantalla
  lv_obj_set_style_text_align(song_title, LV_TEXT_ALIGN_LEFT, 0);
  lv_obj_align(song_title, LV_ALIGN_CENTER, 0, 85);
  lv_obj_set_style_text_color(song_title, lv_color_hex(0xFFFFFF), 0);

  artist = lv_label_create(lv_screen_active());
  lv_label_set_long_mode(artist, LV_LABEL_LONG_SCROLL_CIRCULAR); // si el nombre es mas largo que el ancho de la pantalla va rotando el string para que se vea completo
  lv_label_set_text(artist, "Artista: Desconocido");
  lv_obj_set_width(artist, SCREEN_HEIGHT-10); //El ancho que puede ocupar el texto es igual al alto de la pantalla
  lv_obj_set_style_text_align(artist, LV_TEXT_ALIGN_LEFT, 0);
  lv_obj_align(artist, LV_ALIGN_CENTER, 0, 105);
  lv_obj_set_style_text_color(artist, lv_color_hex(0xb3b3b3), 0);

  progress = lv_label_create(lv_screen_active());
  lv_label_set_text(progress, "00:00");
  lv_obj_set_width(progress, lv_pct(15)); //El ancho que puede ocupar el texto es igual al alto de la pantalla
  lv_obj_set_style_text_align(progress, LV_TEXT_ALIGN_LEFT, 0);
  lv_obj_align(progress, LV_ALIGN_BOTTOM_MID, -120, -55);
  lv_obj_set_style_text_color(progress, lv_color_hex(0xb3b3b3), 0);

  progress_bar = lv_bar_create(lv_screen_active());
  lv_bar_set_start_value(progress_bar, 0, LV_ANIM_OFF);
  lv_obj_set_height(progress_bar, 4);
  lv_obj_set_width(progress_bar, lv_pct(60));
  lv_obj_set_x(progress_bar, 0);
  lv_obj_set_y(progress_bar, -60);
  lv_obj_set_align(progress_bar, LV_ALIGN_BOTTOM_MID);
  lv_obj_set_style_bg_color(progress_bar, lv_color_hex(0x535353), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_opa(progress_bar, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
  
  lv_obj_set_style_bg_color(progress_bar, lv_color_hex(0xFFFFFF), LV_PART_INDICATOR | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_opa(progress_bar, 255, LV_PART_INDICATOR | LV_STATE_DEFAULT);
  lv_bar_set_value(progress_bar, 0, LV_ANIM_ON);

  duration = lv_label_create(lv_screen_active());
  lv_label_set_text(duration, "00:00");
  lv_obj_set_width(duration, lv_pct(15)); //El ancho que puede ocupar el texto es igual al alto de la pantalla
  lv_obj_set_style_text_align(duration, LV_TEXT_ALIGN_LEFT, 0);
  lv_obj_align(duration, LV_ALIGN_BOTTOM_MID, 135, -55);
  lv_obj_set_style_text_color(duration, lv_color_hex(0xb3b3b3), 0);
  
  lv_obj_t * btn_label;

  lv_obj_t * prev_button = lv_button_create(lv_screen_active());
  lv_obj_add_event_cb(prev_button, event_handler_prev_button, LV_EVENT_ALL, NULL);
  lv_obj_align(prev_button, LV_ALIGN_CENTER, 25, 0);
  lv_obj_remove_flag(prev_button, LV_OBJ_FLAG_PRESS_LOCK);
  lv_obj_set_style_bg_opa(prev_button, LV_OPA_TRANSP, 0);
  lv_obj_set_size(prev_button, 35, 35);
  lv_obj_set_style_border_width(prev_button, 0, 0);  // Remove border
  lv_obj_set_style_shadow_width(prev_button, 0, 0);  // Remove shadow

  btn_label = lv_label_create(prev_button);
  lv_label_set_text(btn_label, LV_SYMBOL_PREV);
  lv_obj_set_style_text_color(btn_label, lv_color_hex(0xb3b3b3), 0);
  lv_obj_center(btn_label);

  play_pause_button = lv_button_create(lv_screen_active());
  lv_obj_add_event_cb(play_pause_button, event_handler_play_pause_button, LV_EVENT_ALL, NULL);
  lv_obj_align(play_pause_button, LV_ALIGN_CENTER, 80, 0);
  lv_obj_remove_flag(play_pause_button, LV_OBJ_FLAG_PRESS_LOCK);
  lv_obj_set_style_bg_color(play_pause_button, lv_color_hex(0xffffff), 0);
  lv_obj_set_style_bg_opa(play_pause_button, LV_OPA_COVER, 0);
  lv_obj_set_size(play_pause_button, 35, 35);
  lv_obj_set_style_radius(play_pause_button, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_shadow_width(play_pause_button, 0, 0);

  btn_label = lv_label_create(play_pause_button);
  lv_label_set_text(btn_label, LV_SYMBOL_PLAY);
  lv_obj_set_style_text_color(btn_label, lv_color_hex(0x000000), 0);
  lv_obj_center(btn_label);

  lv_obj_t * next_button = lv_button_create(lv_screen_active());
  lv_obj_add_event_cb(next_button, event_handler_next_button, LV_EVENT_ALL, NULL);
  lv_obj_align(next_button, LV_ALIGN_CENTER, 135, 0);
  lv_obj_remove_flag(next_button, LV_OBJ_FLAG_PRESS_LOCK);
  lv_obj_set_style_bg_opa(next_button, LV_OPA_TRANSP, 0);
  lv_obj_set_size(next_button, 35, 35);
  lv_obj_set_style_border_width(next_button, 0, 0);  // Remove border
  lv_obj_set_style_shadow_width(next_button, 0, 0);  // Remove shadow

  btn_label = lv_label_create(next_button);
  lv_label_set_text(btn_label, LV_SYMBOL_NEXT);
  lv_obj_set_style_text_color(btn_label, lv_color_hex(0xb3b3b3), 0);
  lv_obj_center(btn_label);
}

void setup() {
  Serial.begin(115200);
  
  String LVGL_Arduino = String("LVGL Library Version: ") + lv_version_major() + "." + lv_version_minor() + "." + lv_version_patch();
  Serial.println(LVGL_Arduino);
  
  pinMode(BOOT_BUTTON, INPUT_PULLUP);
  ledController = RGBLedController();

  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS initialisation failed!");
    while (1) yield(); // Stay here twiddling thumbs waiting
  }

  tft.begin();
  tft.fillScreen(TFT_BLACK);
  tft.setRotation(2);

  TJpgDec.setJpgScale(2);
  TJpgDec.setSwapBytes(true);
  TJpgDec.setCallback(tft_output);

  screenSetUp();

  config = Config::load();

  if (!config.hasWifi() || !connectToWifi(config)) {
    startSetupMode();
    return;
  }

  // La pagina de configuracion queda disponible en la red local mientras el reproductor funciona
  portal.startOnNetwork();
  // El QR usa la IP porque siempre funciona. En el texto va primero el nombre, que es mas facil
  // de escribir (.local no anda en algunos Android, por eso tambien se muestra la IP)
  String portalUrl = "http://" + WiFi.localIP().toString();
  String portalAddress = "http://" + String(ConfigPortal::HOSTNAME) + ".local\n(o " + portalUrl + ")";

  if (!config.hasSpotifyApp() || !config.hasRefreshToken()) {
    showInfoScreen("Vincular Spotify",
      "Escanea el QR o abri desde un dispositivo en la misma red WiFi:\n\n" + portalAddress, portalUrl);
    return;
  }

  if (!SpotifyAuth::refreshAccessToken(config, accessToken)) {
    showInfoScreen("Error con Spotify",
      "No se pudo obtener el token. Volve a vincular la cuenta en:\n\n" + portalAddress, portalUrl);
    return;
  }

  lv_obj_clean(lv_screen_active());

  // Function to draw the GUI (text, buttons and sliders)
  drawMainGui();

  lv_timer_create(updateScreen, 5000, NULL);
  lv_timer_create(updateProgressBar, 1000, NULL);
}

void loop() {
  portal.handle();
  lv_timer_handler();  // let the GUI do its work
  delay(5);
}
