#include "fsm/CommandFsm.h"

namespace {
  constexpr auto DELIM = "\r\n";
}

namespace fsm {

bool
oneStepFsm(
  io::Socket &controlSocket,
  const std::string &command
) {
  const auto commandWithDelim(command + DELIM);
  size_t n = controlSocket.sendString(commandWithDelim);
  if (n < commandWithDelim.size()) {
    return false;
  }

  const auto maybeResponse = controlSocket.readUntil(DELIM);
  if (!maybeResponse || maybeResponse->size() == 0) {
    return false;
  }

  return (*maybeResponse)[0] == '2';
}

}
