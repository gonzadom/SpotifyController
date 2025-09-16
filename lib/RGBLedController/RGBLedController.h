#ifndef RGBLEDCONTROLLER_H
#define RGBLEDCONTROLLER_H

#include <Arduino.h>

class RGBLedController {

  private:
    int redPin;
    int greenPin;
    int bluePin;

    void setColor(int red, int green, int blue);

  public:
    void turnOffLed();

    void setLedRed();
    void setLedGreen();

    RGBLedController();

    ~RGBLedController();
};

#endif