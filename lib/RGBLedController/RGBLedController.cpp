#include "RGBLedController.h"

//========= Back Led =========
#define CYD_LED_BLUE 17
#define CYD_LED_RED 4
#define CYD_LED_GREEN 16

void RGBLedController::setColor(int red, int green, int blue) {
    digitalWrite(redPin, red);
    digitalWrite(greenPin, green);
    digitalWrite(bluePin, blue);
}

void RGBLedController::setLedRed() {
    setColor(LOW, HIGH, HIGH);
}

void RGBLedController::setLedGreen() {
    setColor(HIGH, LOW, HIGH);
}

void RGBLedController::turnOffLed() {
    setColor(HIGH, HIGH, HIGH);
}

RGBLedController::RGBLedController() {
    redPin = CYD_LED_RED;
    greenPin = CYD_LED_GREEN;
    bluePin = CYD_LED_BLUE;

    pinMode(redPin, OUTPUT);
    pinMode(greenPin, OUTPUT);
    pinMode(bluePin, OUTPUT);

    turnOffLed();
}

RGBLedController::~RGBLedController() {}