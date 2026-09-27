#ifndef SPOTIFY_AUTH_H
#define SPOTIFY_AUTH_H

#include <Arduino.h>
#include "Config.h"

namespace SpotifyAuth {
  // Tiene que estar registrada tal cual en el dashboard de la app de Spotify.
  // Spotify no permite redirect http a IPs de la red local, asi que se usa loopback:
  // el navegador no va a cargar la pagina, pero la URL trae el codigo que necesitamos.
  extern const char* const REDIRECT_URI;

  String authorizeUrl(const String& clientId);

  // Saca el parametro "code" de la URL pegada (tambien acepta el codigo solo)
  String extractCode(const String& pastedUrl);

  // Cambia el codigo de autorizacion por un refresh token y lo guarda en cfg
  bool exchangeCode(AppConfig& cfg, const String& code, String& error);

  // Pide un access token nuevo. Si Spotify rota el refresh token, lo guarda
  bool refreshAccessToken(AppConfig& cfg, String& accessToken);
}

#endif
