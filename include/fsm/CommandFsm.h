#ifndef FSM_ONESTEPFSM_H
#define FSM_ONESTEPFSM_H

#include <string>
#include <utility>
#include <functional>

#include "io/Socket.h"

namespace fsm {

// Using std::function because (1) I want to keep the implementations
// of these Fsms in a cpp file, so can't easily use templates, and (2)
// I want to be able to pass lambdas which have captures, so can't
// use function pointers.
using Callback = std::function<void()>;

bool
oneStepFsm(
  io::Socket &controlSocket,
  const std::string &command
);

std::optional<std::pair<std::string, std::string>>
pasvFsm(io::Socket &controlSocket);

std::optional<std::string>
directoryFsm(io::Socket &controlSocket, const std::optional<std::string> &path);

bool
twoStepFsm(
  io::Socket &controlSocket,
  const std::string &command,
  const Callback &onPreliminaryReply
);

}

#endif
