#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// Configuracion que se carga desde el portal web y se guarda en la flash (NVS)
struct AppConfig {
  String wifiSsid;
  String wifiPassword;
  String clientId;
  String refreshToken;

  bool hasWifi() const { return wifiSsid.length() > 0; }
  bool hasSpotifyApp() const { return clientId.length() > 0; }
  bool hasRefreshToken() const { return refreshToken.length() > 0; }
};

namespace Config {
  AppConfig load();
  void save(const AppConfig& cfg);
  void clear();
}

#endif
