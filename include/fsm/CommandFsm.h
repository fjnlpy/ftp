#ifndef FSM_ONESTEPFSM_H
#define FSM_ONESTEPFSM_H

#include <string>
#include <utility>

#include "io/Socket.h"

namespace fsm {

bool
oneStepFsm(
  io::Socket &controlSocket,
  const std::string &command
);

std::optional<std::pair<std::string, std::string>>
pasvFsm(io::Socket &controlSocket);

std::optional<std::string>
directoryFsm(io::Socket &controlSocket, const std::optional<std::string> &path);

}

#endif
