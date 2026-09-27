#include "SpotifyAuth.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>

const char* const SpotifyAuth::REDIRECT_URI = "http://127.0.0.1:8888/callback";

static const char* SCOPES = "user-read-currently-playing user-read-playback-state user-modify-playback-state";
static const char* TOKEN_URL = "https://accounts.spotify.com/api/token";

static String urlEncode(const String& value) {
  const char* hex = "0123456789ABCDEF";
  String encoded;
  encoded.reserve(value.length() * 3);

  for (size_t i = 0; i < value.length(); i++) {
    char c = value[i];
    if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') {
      encoded += c;
    } else {
      encoded += '%';
      encoded += hex[(c >> 4) & 0xF];
      encoded += hex[c & 0xF];
    }
  }
  return encoded;
}

// Hace el POST al endpoint de tokens de Spotify y deja la respuesta parseada en doc
static bool postTokenRequest(const AppConfig& cfg, const String& grantParams, JsonDocument& doc, String& error) {
  HTTPClient http;
  http.begin(TOKEN_URL);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  String body = grantParams + "&client_id=" + urlEncode(cfg.clientId) + "&client_secret=" + urlEncode(cfg.clientSecret);
  int httpCode = http.POST(body);
  String response = http.getString();
  http.end();

  DeserializationError jsonError = deserializeJson(doc, response);
  if (httpCode != 200 || jsonError) {
    error = "HTTP " + String(httpCode);
    if (!jsonError && doc["error_description"].is<const char*>()) {
      error += ": " + doc["error_description"].as<String>();
    }
    return false;
  }
  return true;
}

String SpotifyAuth::authorizeUrl(const String& clientId) {
  return String("https://accounts.spotify.com/authorize?response_type=code")
    + "&client_id=" + urlEncode(clientId)
    + "&scope=" + urlEncode(SCOPES)
    + "&redirect_uri=" + urlEncode(REDIRECT_URI);
}

String SpotifyAuth::extractCode(const String& pastedUrl) {
  String url = pastedUrl;
  url.trim();

  int start = url.indexOf("?code=");
  if (start < 0) start = url.indexOf("&code=");

  if (start < 0) {
    // Si no parece una URL asumimos que pegaron el codigo solo
    bool looksLikeUrl = url.indexOf('/') >= 0 || url.indexOf('=') >= 0;
    return looksLikeUrl ? "" : url;
  }

  start += 6;
  int end = url.indexOf('&', start);
  return end < 0 ? url.substring(start) : url.substring(start, end);
}

bool SpotifyAuth::exchangeCode(AppConfig& cfg, const String& code, String& error) {
  JsonDocument doc;
  String params = "grant_type=authorization_code&code=" + urlEncode(code) + "&redirect_uri=" + urlEncode(REDIRECT_URI);

  if (!postTokenRequest(cfg, params, doc, error)) {
    return false;
  }

  const char* refreshToken = doc["refresh_token"];
  if (!refreshToken) {
    error = "Spotify no devolvio un refresh token";
    return false;
  }

  cfg.refreshToken = refreshToken;
  return true;
}

bool SpotifyAuth::refreshAccessToken(AppConfig& cfg, String& accessToken) {
  JsonDocument doc;
  String error;

  if (!postTokenRequest(cfg, "grant_type=refresh_token&refresh_token=" + urlEncode(cfg.refreshToken), doc, error)) {
    Serial.println("Error al refrescar el token: " + error);
    return false;
  }

  accessToken = doc["access_token"].as<String>();

  // Spotify a veces devuelve un refresh token nuevo, y el viejo deja de servir
  const char* newRefreshToken = doc["refresh_token"];
  if (newRefreshToken && cfg.refreshToken != newRefreshToken) {
    cfg.refreshToken = newRefreshToken;
    Config::save(cfg);
  }

  Serial.println("Token refrescado");
  return true;
}
