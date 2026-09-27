#include "SpotifyAuth.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <mbedtls/sha256.h>

const char* const SpotifyAuth::REDIRECT_URI = "https://www.google.com/";

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

// Base64 "url safe" sin padding, como pide PKCE
static String base64UrlEncode(const uint8_t* data, size_t len) {
  const char* alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
  String encoded;
  encoded.reserve((len * 4) / 3 + 3);

  for (size_t i = 0; i < len; i += 3) {
    uint32_t chunk = (uint32_t)data[i] << 16;
    if (i + 1 < len) chunk |= (uint32_t)data[i + 1] << 8;
    if (i + 2 < len) chunk |= data[i + 2];

    encoded += alphabet[(chunk >> 18) & 0x3F];
    encoded += alphabet[(chunk >> 12) & 0x3F];
    if (i + 1 < len) encoded += alphabet[(chunk >> 6) & 0x3F];
    if (i + 2 < len) encoded += alphabet[chunk & 0x3F];
  }
  return encoded;
}

// PKCE: el code verifier es un valor aleatorio que solo conoce la CYD. Al pedir la autorizacion
// se manda su hash (code challenge) y al canjear el codigo se manda el valor original,
// asi Spotify sabe que es el mismo dispositivo sin necesitar el client secret.
// Se genera una vez por arranque (despues de vincular la cuenta la CYD se reinicia)
static String codeVerifier() {
  static String verifier;
  if (verifier.length() == 0) {
    uint8_t random[32];
    esp_fill_random(random, sizeof(random));
    verifier = base64UrlEncode(random, sizeof(random));
  }
  return verifier;
}

static String codeChallenge() {
  String verifier = codeVerifier();
  uint8_t hash[32];
  mbedtls_sha256_ret((const unsigned char*)verifier.c_str(), verifier.length(), hash, 0);
  return base64UrlEncode(hash, sizeof(hash));
}

// Hace el POST al endpoint de tokens de Spotify y deja la respuesta parseada en doc
static bool postTokenRequest(const AppConfig& cfg, const String& grantParams, JsonDocument& doc, String& error) {
  HTTPClient http;
  http.begin(TOKEN_URL);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  String body = grantParams + "&client_id=" + urlEncode(cfg.clientId);
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
    + "&redirect_uri=" + urlEncode(REDIRECT_URI)
    + "&code_challenge_method=S256"
    + "&code_challenge=" + codeChallenge();
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
  String params = "grant_type=authorization_code&code=" + urlEncode(code) + "&redirect_uri=" + urlEncode(REDIRECT_URI)
    + "&code_verifier=" + codeVerifier();

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

  // Con PKCE Spotify devuelve un refresh token nuevo en cada refresco, y el viejo deja de servir
  const char* newRefreshToken = doc["refresh_token"];
  if (newRefreshToken && cfg.refreshToken != newRefreshToken) {
    cfg.refreshToken = newRefreshToken;
    Config::save(cfg);
  }

  Serial.println("Token refrescado");
  return true;
}
