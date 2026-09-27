#include "ConfigPortal.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include "SpotifyAuth.h"

const char* const ConfigPortal::AP_SSID = "SpotifyController";
const char* const ConfigPortal::HOSTNAME = "spotify";

static const unsigned long AP_RETRY_MS = 3 * 60 * 1000;

static const char PAGE_HEAD[] PROGMEM = R"html(<!DOCTYPE html>
<html lang="es"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Spotify Controller</title>
<style>
body{font-family:system-ui,sans-serif;background:#121212;color:#fff;margin:0;padding:16px}
main{max-width:480px;margin:auto}
h1{color:#1db954;font-size:1.5em}
h2{font-size:1.1em;margin-top:28px;border-bottom:1px solid #333;padding-bottom:6px}
label{display:block;margin:12px 0 4px;color:#b3b3b3;font-size:.9em}
input,textarea{width:100%;box-sizing:border-box;padding:10px;border-radius:6px;border:1px solid #333;background:#282828;color:#fff;font-size:1em}
textarea{height:90px}
button{margin-top:14px;padding:12px 20px;border:0;border-radius:24px;background:#1db954;color:#000;font-weight:bold;font-size:1em;width:100%}
button.danger{background:#e22134;color:#fff}
a{color:#1db954}
code{background:#282828;padding:2px 6px;border-radius:4px;word-break:break-all}
.hint{color:#b3b3b3;font-size:.9em}
.status{background:#282828;border-radius:8px;padding:10px 14px}
.ok{color:#1db954}.no{color:#e22134}
</style></head><body><main>
)html";

static const char PAGE_TAIL[] PROGMEM = "</main></body></html>";

static String htmlEscape(const String& text) {
  String escaped;
  escaped.reserve(text.length());
  for (size_t i = 0; i < text.length(); i++) {
    char c = text[i];
    switch (c) {
      case '&': escaped += "&amp;"; break;
      case '<': escaped += "&lt;"; break;
      case '>': escaped += "&gt;"; break;
      case '"': escaped += "&quot;"; break;
      default: escaped += c;
    }
  }
  return escaped;
}

ConfigPortal::ConfigPortal(AppConfig& cfg) : cfg(cfg), server(80) {}

void ConfigPortal::startAccessPoint() {
  apMode = true;

  // Modo AP+STA para poder escanear redes mientras la red propia esta activa
  WiFi.disconnect();
  WiFi.mode(WIFI_AP_STA);
  scanNetworks();

  WiFi.softAP(AP_SSID);
  // Todas las consultas DNS apuntan a la CYD, asi el celu abre el portal solo
  dns.start(53, "*", WiFi.softAPIP());
  apStartedAt = millis();

  Serial.println("Portal de configuracion en la red " + String(AP_SSID) + " -> http://" + WiFi.softAPIP().toString());
  begin();
}

void ConfigPortal::startOnNetwork() {
  apMode = false;
  scanNetworks();

  if (MDNS.begin(HOSTNAME)) {
    MDNS.addService("http", "tcp", 80);
  }

  Serial.println("Portal de configuracion en http://" + WiFi.localIP().toString() + " (http://" + String(HOSTNAME) + ".local)");
  begin();
}

void ConfigPortal::begin() {
  server.on("/", HTTP_GET, [this]() { handleRoot(); });
  server.on("/save", HTTP_POST, [this]() { handleSave(); });
  server.on("/auth", HTTP_POST, [this]() { handleAuth(); });
  server.on("/reset", HTTP_POST, [this]() { handleReset(); });
  server.onNotFound([this]() { handleNotFound(); });
  server.begin();
  started = true;
}

void ConfigPortal::handle() {
  if (!started) return;

  if (apMode) dns.processNextRequest();
  server.handleClient();

  // Se reinicia un rato despues de responder, para que la pagina llegue al navegador
  if (restartAt && millis() > restartAt) {
    ESP.restart();
  }

  // Si ya hay un WiFi guardado (por ejemplo se corto la luz y el router tardo en volver)
  // y nadie esta usando el portal, se reinicia para volver a intentar la conexion
  if (apMode && cfg.hasWifi() && WiFi.softAPgetStationNum() == 0 && millis() - apStartedAt > AP_RETRY_MS) {
    Serial.println("Nadie usa el portal, reintentando conectar al WiFi guardado");
    ESP.restart();
  }
}

void ConfigPortal::scheduleRestart() {
  restartAt = millis() + 2000;
}

void ConfigPortal::scanNetworks() {
  Serial.println("Buscando redes WiFi...");
  int count = WiFi.scanNetworks();

  networkOptions = "";
  for (int i = 0; i < count; i++) {
    String option = "<option value=\"" + htmlEscape(WiFi.SSID(i)) + "\">";
    if (WiFi.SSID(i).length() > 0 && networkOptions.indexOf(option) < 0) {
      networkOptions += option;
    }
  }
  WiFi.scanDelete();
}

void ConfigPortal::sendPage(const String& body) {
  String page = FPSTR(PAGE_HEAD);
  page += body;
  page += FPSTR(PAGE_TAIL);
  server.send(200, "text/html; charset=utf-8", page);
}

void ConfigPortal::sendMessage(const String& title, const String& text, bool backLink) {
  String body = "<h1>" + title + "</h1><p>" + text + "</p>";
  if (backLink) body += "<p><a href=\"/\">&larr; Volver</a></p>";
  sendPage(body);
}

void ConfigPortal::handleRoot() {
  if (server.hasArg("scan")) scanNetworks();

  bool wifiConnected = WiFi.status() == WL_CONNECTED;
  String body = "<h1>Spotify Controller</h1><div class=\"status\">";

  body += "<p>WiFi: ";
  body += wifiConnected ? "<span class=\"ok\">conectado a " + htmlEscape(WiFi.SSID()) + "</span>"
                        : "<span class=\"no\">sin conexion</span>";
  body += "</p><p>Spotify: ";
  body += cfg.hasRefreshToken() ? "<span class=\"ok\">cuenta vinculada</span>" : "<span class=\"no\">sin vincular</span>";
  body += "</p></div>";

  // Paso 1 y 2: WiFi y app de Spotify
  body += "<form method=\"post\" action=\"/save\">";
  body += "<h2>1. WiFi</h2>";
  body += "<label>Red</label><input name=\"ssid\" list=\"nets\" required value=\"" + htmlEscape(cfg.wifiSsid) + "\">";
  body += "<datalist id=\"nets\">" + networkOptions + "</datalist>";
  body += "<p class=\"hint\"><a href=\"/?scan=1\">Buscar redes de nuevo</a></p>";
  body += "<label>Contrase&ntilde;a</label><input name=\"pass\" type=\"password\"";
  if (cfg.hasWifi()) body += " placeholder=\"(dejar vacio para no cambiarla)\"";
  body += ">";

  body += "<h2>2. App de Spotify</h2>";
  body += "<p class=\"hint\">Cre&aacute; una app en <a href=\"https://developer.spotify.com/dashboard\" target=\"_blank\">developer.spotify.com/dashboard</a> ";
  body += "(tildando <b>Web API</b>) y agreg&aacute; esta Redirect URI:<br><code>" + String(SpotifyAuth::REDIRECT_URI) + "</code></p>";
  body += "<label>Client ID</label><input name=\"client_id\" value=\"" + htmlEscape(cfg.clientId) + "\">";
  body += "<button>Guardar</button></form>";

  // Paso 3: vincular la cuenta (necesita que la CYD tenga internet)
  body += "<h2>3. Vincular cuenta de Spotify</h2>";
  if (apMode) {
    body += "<p class=\"hint\">Este paso se hace cuando la CYD ya est&eacute; conectada a tu WiFi. ";
    body += "Guard&aacute; los datos de arriba y segu&iacute; las instrucciones de la pantalla.</p>";
  } else if (!cfg.hasSpotifyApp()) {
    body += "<p class=\"hint\">Primero carg&aacute; el Client ID.</p>";
  } else {
    body += "<ol class=\"hint\">";
    body += "<li><a href=\"" + htmlEscape(SpotifyAuth::authorizeUrl(cfg.clientId)) + "\" target=\"_blank\">Abr&iacute; este link</a> e inici&aacute; sesi&oacute;n.</li>";
    body += "<li>Te va a llevar a Google. Copi&aacute; la direcci&oacute;n completa de la barra del navegador (tiene <code>?code=</code>).</li>";
    body += "<li>Pegala ac&aacute;:</li></ol>";
    body += "<form method=\"post\" action=\"/auth\"><textarea name=\"url\" required placeholder=\"https://www.google.com/?code=...\"></textarea>";
    body += "<button>Vincular</button></form>";
  }

  body += "<h2>Otros</h2>";
  body += "<form method=\"post\" action=\"/reset\" onsubmit=\"return confirm('Borrar toda la configuracion?')\">";
  body += "<button class=\"danger\">Borrar configuraci&oacute;n</button></form>";

  sendPage(body);
}

void ConfigPortal::handleSave() {
  String ssid = server.arg("ssid");
  String pass = server.arg("pass");
  String clientId = server.arg("client_id");
  ssid.trim();
  clientId.trim();

  if (ssid.length() == 0) {
    sendMessage("Error", "Falta el nombre de la red WiFi.", true);
    return;
  }

  // Contraseña vacia = no cambiarla, salvo que sea otra red (puede ser una red abierta)
  bool wifiChanged = ssid != cfg.wifiSsid || pass.length() > 0;
  if (wifiChanged) {
    cfg.wifiSsid = ssid;
    cfg.wifiPassword = pass;
  }

  bool spotifyChanged = clientId != cfg.clientId;
  if (spotifyChanged) {
    cfg.clientId = clientId;
    // Con otra app el refresh token viejo no sirve, hay que volver a vincular
    cfg.refreshToken = "";
  }

  Config::save(cfg);

  if (apMode || wifiChanged) {
    sendMessage("Guardado", "La CYD se va a reiniciar y conectar a <b>" + htmlEscape(ssid) + "</b>. "
                "Volv&eacute; a conectar el celu a tu WiFi y segu&iacute; las instrucciones de la pantalla.", false);
    scheduleRestart();
    return;
  }

  // Sin cambios de WiFi seguimos en la misma pagina para hacer el paso 3
  server.sendHeader("Location", "/");
  server.send(303);
}

void ConfigPortal::handleAuth() {
  if (apMode || !cfg.hasSpotifyApp()) {
    sendMessage("Error", "Primero configur&aacute; el WiFi y la app de Spotify.", true);
    return;
  }

  String code = SpotifyAuth::extractCode(server.arg("url"));
  if (code.length() == 0) {
    sendMessage("Error", "No encontr&eacute; el c&oacute;digo en la direcci&oacute;n que pegaste. "
                "Tiene que ser la URL completa, empezando con <code>" + String(SpotifyAuth::REDIRECT_URI) + "?code=</code>", true);
    return;
  }

  String error;
  if (!SpotifyAuth::exchangeCode(cfg, code, error)) {
    sendMessage("Error", "Spotify rechaz&oacute; el c&oacute;digo (" + htmlEscape(error) + "). "
                "Cada c&oacute;digo sirve una sola vez: volv&eacute; a abrir el link e intent&aacute; de nuevo.", true);
    return;
  }

  Config::save(cfg);
  sendMessage("&iexcl;Listo!", "Tu cuenta de Spotify qued&oacute; vinculada. La CYD se reinicia y arranca el reproductor.", false);
  scheduleRestart();
}

void ConfigPortal::handleReset() {
  Config::clear();
  sendMessage("Configuraci&oacute;n borrada", "La CYD se reinicia en modo configuraci&oacute;n. "
              "Conectate a la red <b>" + String(AP_SSID) + "</b> para configurarla de nuevo.", false);
  scheduleRestart();
}

void ConfigPortal::handleNotFound() {
  // En modo AP cualquier URL (incluidas las de deteccion de portal cautivo) lleva a la config
  if (apMode) {
    server.sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/");
    server.send(302);
    return;
  }
  server.send(404, "text/plain", "No encontrado");
}
