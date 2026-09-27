// Fetch a file from the URL given and save it in SPIFFS
// Return true if the file is available (already existed or was downloaded completely)
bool getFile(String url, String filename) {

  // If it exists then no need to fetch it
  if (SPIFFS.exists(filename) == true) {
    Serial.println("Found " + filename);
    return true;
  }

  bool downloaded = false;

  Serial.println("Downloading "  + filename + " from " + url);

  // Check WiFi connection
  if ((WiFi.status() == WL_CONNECTED)) {

    Serial.print("[HTTP] begin...\n");

#ifdef ARDUINO_ARCH_ESP8266
    std::unique_ptr<BearSSL::WiFiClientSecure>client(new BearSSL::WiFiClientSecure);
    client -> setInsecure();
    HTTPClient http;
    http.begin(*client, url);
#else
    HTTPClient http;
    // Configure server and url
    http.begin(url);
#endif

    Serial.print("[HTTP] GET...\n");
    // Start connection and send HTTP header
    int httpCode = http.GET();
    if (httpCode > 0) {
      fs::File f = SPIFFS.open(filename, "w+");
      if (!f) {
        Serial.println("file open failed");
        http.end();
        return false;
      }
      // HTTP header has been send and Server response header has been handled
      Serial.printf("[HTTP] GET... code: %d\n", httpCode);

      // File found at server
      if (httpCode == HTTP_CODE_OK) {

        // Get length of document (is -1 when Server sends no Content-Length header)
        int total = http.getSize();
        int len = total;

        // Create buffer for read
        const size_t buff_size = 2048;
        uint8_t *buff = (uint8_t *)calloc(buff_size, sizeof(uint8_t)); //original era 128
        if (!buff) {
          Serial.print("Memory error, could not allocate buffer.\n");
          f.close();
          SPIFFS.remove(filename);
          http.end();
          return false;
        }

        // Get tcp stream
        WiFiClient * stream = http.getStreamPtr();

        // Read all data from server
        while (http.connected() && (len > 0 || len == -1)) {
          // Get available data size
          size_t size = stream->available();

          if (size) {
            // Read up to 2048 bytes
            int c = stream->readBytes(buff, min(size, buff_size));

            // Write it to file
            f.write(buff, c);

            // Calculate remaining bytes
            if (len > 0) {
              len -= c;
            }
          }
          yield();
        }

        free(buff);

        // Si el servidor mando el tamaño, tiene que haber llegado todo
        downloaded = total <= 0 || len == 0;

        Serial.println();
        Serial.print("[HTTP] connection closed or file end.\n");
      }
      f.close();

      // No dejar archivos a medio bajar, se reintenta en el proximo ciclo
      if (!downloaded) {
        Serial.println("Descarga incompleta de " + filename);
        SPIFFS.remove(filename);
      }
    }
    else {
      Serial.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
    }
    http.end();
  }
  return downloaded;
}
