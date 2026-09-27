# Spotify Controller

## Descripcion
Un projecto que hacia tiempo queria realizar.

La idea es imitar una especie de "Spotify car thing" utilizando la API de spotify junto con un Cheap yellow display (CYD) (Esp32)
Programado utilizando las herramientas de desarrollo provistas por la extension de VScode PlatformIO.

## Configuracion

Ya no hace falta un `secrets.h`: el WiFi y las credenciales de Spotify se cargan desde una pagina web que sirve la CYD y quedan guardadas en la flash.

1. **Crear la app de Spotify**: en [developer.spotify.com/dashboard](https://developer.spotify.com/dashboard) crear una app con la API **Web API** y agregar como Redirect URI:
   `http://127.0.0.1:8888/callback`
2. **Configurar el WiFi**: la primera vez (o si no puede conectarse) la CYD crea la red `SpotifyController`. Escanear el QR de la pantalla o conectarse a mano: se abre la pagina de configuracion (si no, entrar a `192.168.4.1`). Cargar la red WiFi, el Client ID y el Client Secret.
3. **Vincular la cuenta**: la CYD se reinicia, se conecta al WiFi y muestra una direccion (tambien un QR). Abrirla desde un dispositivo en la misma red, tocar el link para iniciar sesion en Spotify y pegar la URL `http://127.0.0.1:8888/callback?code=...` a la que redirige (la pagina no carga, es normal).

La pagina de configuracion sigue disponible en `http://<ip de la CYD>` o `http://spotify.local` mientras el reproductor funciona.
Para volver a la configuracion con la red propia, mantener apretado el boton **BOOT** mientras dice "Conectando...".

La configuracion de LVGL (`include/lv_conf.h`) y de la pantalla para TFT_eSPI (`build_flags` en `platformio.ini`) estan en el repo, asi que no hace falta tocar archivos dentro de `.pio`.
