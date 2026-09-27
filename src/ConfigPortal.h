#ifndef CONFIG_PORTAL_H
#define CONFIG_PORTAL_H

#include <Arduino.h>
#include <WebServer.h>
#include <DNSServer.h>
#include "Config.h"

// Pagina web para cargar el WiFi, las credenciales de Spotify y vincular la cuenta
class ConfigPortal {
  public:
    static const char* const AP_SSID;
    static const char* const HOSTNAME;

    explicit ConfigPortal(AppConfig& cfg);

    // Crea la red WiFi propia de la CYD con portal cautivo (cuando no hay WiFi configurado)
    void startAccessPoint();
    // Sirve la pagina en la red WiFi a la que ya esta conectada la CYD
    void startOnNetwork();
    // Llamar en cada loop()
    void handle();

  private:
    AppConfig& cfg;
    WebServer server;
    DNSServer dns;
    bool apMode = false;
    bool started = false;
    unsigned long restartAt = 0;
    unsigned long apStartedAt = 0;
    String networkOptions;

    void begin();
    void scanNetworks();
    void scheduleRestart();

    void handleRoot();
    void handleSave();
    void handleAuth();
    void handleReset();
    void handleNotFound();

    void sendPage(const String& body);
    void sendMessage(const String& title, const String& text, bool backLink);
};

#endif
