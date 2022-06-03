#ifndef FSM_ONESTEPFSM_H
#define FSM_ONESTEPFSM_H

#include <string>

#include "io/Socket.h"

namespace fsm {

bool
oneStepFsm(
  io::Socket &controlSocket,
  const std::string &command
);

}

#endif
