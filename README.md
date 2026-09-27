# Spotify Controller

## Descripcion
Un projecto que hacia tiempo queria realizar.

La idea es imitar una especie de "Spotify car thing" utilizando la API de spotify junto con un Cheap yellow display (CYD) (Esp32)
Programado utilizando las herramientas de desarrollo provistas por la extension de VScode PlatformIO.

## Configuracion

El login con Spotify usa [Authorization Code con PKCE](https://developer.spotify.com/documentation/web-api/tutorials/code-pkce-flow), por lo que solo hace falta el Client ID de la app (que es publico), no el Client Secret.

Ya no hace falta un `secrets.h`: el WiFi y las credenciales de Spotify se cargan desde una pagina web que sirve la CYD y quedan guardadas en la flash.

1. **Crear la app de Spotify**: en [developer.spotify.com/dashboard](https://developer.spotify.com/dashboard) crear una app con la API **Web API** y agregar como Redirect URI:
   `https://www.google.com/`
2. **Configurar el WiFi**: la primera vez (o si no puede conectarse) la CYD crea la red `SpotifyController`. Escanear el QR de la pantalla o conectarse a mano: se abre la pagina de configuracion (si no, entrar a `192.168.4.1`). Cargar la red WiFi y el Client ID de la app.
3. **Vincular la cuenta**: la CYD se reinicia, se conecta al WiFi y muestra una direccion (tambien un QR). Abrirla desde un dispositivo en la misma red, tocar el link para iniciar sesion en Spotify y pegar la URL `https://www.google.com/?code=...` a la que redirige.

La pagina de configuracion sigue disponible en `http://spotify.local` (o la IP que muestra la pantalla) mientras el reproductor funciona. En muchos routers tambien anda `http://spotify`.
Para volver a la configuracion con la red propia, mantener apretado el boton **BOOT** mientras dice "Conectando...".

La configuracion de LVGL (`include/lv_conf.h`) y de la pantalla para TFT_eSPI (`build_flags` en `platformio.ini`) estan en el repo, asi que no hace falta tocar archivos dentro de `.pio`.
