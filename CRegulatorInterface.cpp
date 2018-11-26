/*
 * CPIDtune.cpp
 *
 *  Created on: 17 ????. 2018 ?.
 *      Author: User
 */

#include "CRegulatorInterface.h"
CRegulatorInterface::CRegulatorInterface(CRelayBangBang &RelayPID) :
    retulator_(RelayPID) {

}
bool CRegulatorInterface::deSerialize(const JsonObject& root) {
  retulator_.set_mode(static_cast<CRelayBangBang::mode_t>(root["mode"].as<uint8_t>()));
  if (root.containsKey("setpoint")) {
    retulator_.setpoint_ = root["setpoint"];
  }
  if (root.containsKey("hysteresis")) {
    retulator_.hysteresis_ = root["hysteresis"];
  }

}

bool CRegulatorInterface::serialize(JsonObject& root) const {
  root["mode"] = static_cast<uint8_t>(retulator_.mode_);
  root["input"] = retulator_.input_;
  root["output"] = retulator_.output_;
  root["setpoint"] = retulator_.setpoint_;
  root["hysteresis"] = retulator_.hysteresis_;
}