#ifndef SPOTIFY_AUTH_H
#define SPOTIFY_AUTH_H

#include <Arduino.h>
#include "Config.h"

namespace SpotifyAuth {
  // Tiene que estar registrada tal cual en el dashboard de la app de Spotify.
  // Spotify no permite redirect http a IPs de la red local, asi que se redirige a Google
  // y el usuario copia la URL, que trae el codigo que necesitamos. Con PKCE el codigo
  // no sirve sin el code verifier que solo tiene la CYD, asi que no importa que lo vea Google.
  extern const char* const REDIRECT_URI;

  // Link para iniciar sesion. Usa PKCE, asi no hace falta el client secret
  String authorizeUrl(const String& clientId);

  // Saca el parametro "code" de la URL pegada (tambien acepta el codigo solo)
  String extractCode(const String& pastedUrl);

  // Cambia el codigo de autorizacion por un refresh token y lo guarda en cfg
  bool exchangeCode(AppConfig& cfg, const String& code, String& error);

  // Pide un access token nuevo. Si Spotify rota el refresh token, lo guarda
  bool refreshAccessToken(AppConfig& cfg, String& accessToken);
}

#endif
