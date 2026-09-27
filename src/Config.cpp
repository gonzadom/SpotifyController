#include "Config.h"
#include <Preferences.h>

static const char* PREFS_NAMESPACE = "config";

// getString loguea un error si la clave no existe, por eso se chequea antes
static String readKey(Preferences& prefs, const char* key) {
  return prefs.isKey(key) ? prefs.getString(key, "") : "";
}

AppConfig Config::load() {
  Preferences prefs;
  prefs.begin(PREFS_NAMESPACE, false);

  AppConfig cfg;
  cfg.wifiSsid = readKey(prefs, "ssid");
  cfg.wifiPassword = readKey(prefs, "pass");
  cfg.clientId = readKey(prefs, "client_id");
  cfg.refreshToken = readKey(prefs, "refresh_token");

  prefs.end();
  return cfg;
}

void Config::save(const AppConfig& cfg) {
  Preferences prefs;
  prefs.begin(PREFS_NAMESPACE, false);

  prefs.putString("ssid", cfg.wifiSsid);
  prefs.putString("pass", cfg.wifiPassword);
  prefs.putString("client_id", cfg.clientId);
  // Versiones anteriores guardaban el client secret, con PKCE ya no se usa
  if (prefs.isKey("client_secret")) prefs.remove("client_secret");
  prefs.putString("refresh_token", cfg.refreshToken);

  prefs.end();
  Serial.println("Configuracion guardada");
}

void Config::clear() {
  Preferences prefs;
  prefs.begin(PREFS_NAMESPACE, false);
  prefs.clear();
  prefs.end();
  Serial.println("Configuracion borrada");
}
