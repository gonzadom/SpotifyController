#include <Arduino.h>
#include <WiFi.h>
#include <lvgl.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <TJpg_Decoder.h>
#include <FS.h>
#include "secrets.h"

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

lv_obj_t *progress_bar;

String current_song_id = "";
String current_playing_state = "";
String artworkURL = "";

volatile int32_t progress_ms = 0;
volatile int32_t duration_ms = 1;

//========= Back Led =========
#define CYD_LED_BLUE 17
#define CYD_LED_RED 4
#define CYD_LED_GREEN 16

void setLedRed() {
  digitalWrite(CYD_LED_RED, LOW); 
  digitalWrite(CYD_LED_GREEN, HIGH);
  digitalWrite(CYD_LED_BLUE, HIGH);
}

void setLedGreen() {
  digitalWrite(CYD_LED_RED, HIGH); 
  digitalWrite(CYD_LED_GREEN, LOW);
  digitalWrite(CYD_LED_BLUE, HIGH);
}

void turnOffLed() {
  digitalWrite(CYD_LED_RED, HIGH);
  digitalWrite(CYD_LED_GREEN, HIGH);
  digitalWrite(CYD_LED_BLUE, HIGH);
}

void setUpLed() {
  pinMode(CYD_LED_RED, OUTPUT);
  pinMode(CYD_LED_GREEN, OUTPUT);
  pinMode(CYD_LED_BLUE, OUTPUT);

  turnOffLed();
}

//========= WIFI =========

void connectToWifi(const char* ssid, const char* password) {
  Serial.println("Conectando al WiFi...");

  setLedRed();

  // Connect to Wi-Fi network
  WiFi.begin(ssid, password);

  // Wait for the connection
  int cicles = 0;
  while (WiFi.status() != WL_CONNECTED) {
    if (cicles == 10)
      ESP.restart();
    delay(1000);
    cicles++;
    Serial.println("Conectando...");
  }

  setLedGreen();

  // Once connected, print the local IP address
  Serial.println("Conectado al WiFi!");
  Serial.print("Red: ");
  Serial.println(WiFi.SSID());
  Serial.print("Direccion IP: ");
  Serial.println(WiFi.localIP());
  Serial.print("Fuerza de la señal (RSSI): ");
  Serial.println(WiFi.RSSI());

  delay(1000);

  turnOffLed();
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

//========= Access Token =========
Preferences preferences;

bool tokenSaved() {
  String token = preferences.getString("access_token", "");
  return token.length() > 0;  // Retorna true si el token no está vacío
}

void saveAccessToken(String token) {
  preferences.putString("access_token", token);
  Serial.println("Access Token guardado correctamente.");
}

String readAccessToken() {
  return preferences.getString("access_token", "");
}

String getNewAccessToken() {
  HTTPClient http;
  http.begin("https://accounts.spotify.com/api/token");
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  String body = "grant_type=refresh_token&refresh_token=" + refreshToken + "&client_id=" + clientId + "&client_secret=" + clientSecret;

  int httpCode = http.POST(body);  // Realiza la petición POST

  if (httpCode != 200) {
    Serial.println("Error al refrescar el token");
    return "";
  }

  String response = http.getString();

  JsonDocument doc;
  deserializeJson(doc, response);
    
  Serial.println("Token refrescado");
  return doc["access_token"];

  http.end();
}

void downloadImage(const char* url) {

  if (String(url) == artworkURL) {
    Serial.println("Arte de tapa ya descargado");
    return;
  }
  
  artworkURL = url;
  
  if(SPIFFS.exists("/albumArt.jpg") == true) {
    SPIFFS.remove("/albumArt.jpg");
  }

  getFile(url, "/albumArt.jpg");

  TJpgDec.drawFsJpg(5, 5, "/albumArt.jpg");
}

void updateSongInfo(JsonDocument doc){

  const char* imageUrl = doc["item"]["album"]["images"][1]["url"];
  downloadImage(imageUrl);

  const char* track_name = doc["item"]["name"];
  const char* artist_name = doc["item"]["artists"][0]["name"];
  Serial.println("Canción actual:");
  Serial.println("Nombre: " + String(track_name));
  Serial.println("Artista: " + String(artist_name));
  lv_label_set_text(song_title, String(track_name).c_str());
  lv_label_set_text(artist, String(artist_name).c_str());

  noInterrupts();
  progress_ms = doc["progress_ms"];
  duration_ms = doc["item"]["duration_ms"];
  interrupts();
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
  noInterrupts();
  progress_ms += 1;
  int32_t porcentage = (progress_ms * 100) / duration_ms;
  lv_bar_set_value(progress_bar, porcentage, LV_ANIM_ON);
  interrupts();
}

static void updateScreen(lv_timer_t *timer) {
  HTTPClient http;
  http.begin("https://api.spotify.com/v1/me/player/currently-playing");
  http.addHeader("Authorization", "Bearer " + readAccessToken());

  int httpCode = http.GET();

  if (httpCode == 200) {
    String response = http.getString();

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, response);

    if (error) {
      Serial.println("Error al parsear el JSON: " + String(error.c_str()));
      return;
    }

    noInterrupts();
    progress_ms = doc["progress_ms"];
    duration_ms = doc["item"]["duration_ms"];
    interrupts();

    const char *song_id = doc["item"]["id"];
    String playing_state = doc["is_playing"];
    if (current_song_id == song_id  && current_playing_state == playing_state) {
      http.end();
      Serial.println("La cancion y el estado no cambiaron");
      return;
    }

    if (current_song_id != song_id  && current_playing_state != playing_state) {
      current_song_id = song_id;
      current_playing_state = playing_state;
      Serial.println("La cancion y el estado cambiaron");
      updateSongInfo(doc);
      updatePlayPauseButton();
    }

    if (current_song_id != song_id) {
      current_song_id = song_id;
      Serial.println("La cancion cambio");
      updateSongInfo(doc);
    }
    
    if (current_playing_state != playing_state) {
      current_playing_state = playing_state;
      Serial.println("El estado cambio");
      updatePlayPauseButton();
    }
  } else if (httpCode == 401) {
    saveAccessToken(getNewAccessToken());
    http.end();
    updateScreen(NULL);
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

  http.addHeader("Authorization", "Bearer " + readAccessToken());  // Cabecera con el token de acceso
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
  http.addHeader("Authorization", "Bearer " + readAccessToken());  // Cabecera con el token de acceso
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
  http.addHeader("Authorization", "Bearer " + readAccessToken());  // Cabecera con el token de acceso
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

  progress_bar = lv_bar_create(lv_screen_active());
  lv_bar_set_start_value(progress_bar, 0, LV_ANIM_OFF);
  lv_obj_set_height(progress_bar, 4);
  lv_obj_set_width(progress_bar, lv_pct(70));
  lv_obj_set_x(progress_bar, 0);
  lv_obj_set_y(progress_bar, -60);
  lv_obj_set_align(progress_bar, LV_ALIGN_BOTTOM_MID);
  lv_obj_set_style_bg_color(progress_bar, lv_color_hex(0x535353), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_opa(progress_bar, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
  
  lv_obj_set_style_bg_color(progress_bar, lv_color_hex(0xFFFFFF), LV_PART_INDICATOR | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_opa(progress_bar, 255, LV_PART_INDICATOR | LV_STATE_DEFAULT);
  lv_bar_set_value(progress_bar, 0, LV_ANIM_ON);
  
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
  
  setUpLed();

  connectToWifi(ssid, password);

  preferences.begin("spotify", false);

  if (!tokenSaved()) {
    Serial.println("No se encontró un Access Token. Generando y guardando uno nuevo...");
    saveAccessToken(getNewAccessToken());
  } else {
    Serial.println("Access Token ya guardado: " + readAccessToken());
  }

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

  // Function to draw the GUI (text, buttons and sliders)
  drawMainGui();

  lv_timer_create(updateScreen, 5000, NULL);
  lv_timer_create(updateProgressBar, 1000, NULL);
}

void loop() {
  lv_task_handler();  // let the GUI do its work
  lv_tick_inc(5);     // tell LVGL how much time has passed
  delay(5);           // let this time pass
}