#include "laser.h"
#include "config.h"

void LaserController::begin() {
  ledcSetup(LASER_LEDC_CH, LASER_FREQ_HZ, LASER_RES_BITS);
  ledcAttachPin(LASER_PWM_PIN, LASER_LEDC_CH);
  off();
}

void LaserController::setDuty(uint16_t duty) {
  duty_ = constrain(duty, 0, LASER_MAX_DUTY);
  ledcWrite(LASER_LEDC_CH, duty_);
}

void LaserController::off() {
  duty_ = 0;
  ledcWrite(LASER_LEDC_CH, 0);
}

void LaserController::on(uint16_t duty) {
  setDuty(duty);
}

uint16_t LaserController::getDuty() const {
  return duty_;
}
