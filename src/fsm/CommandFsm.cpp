#include "fsm/CommandFsm.h"

#include <regex>

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

std::optional<std::pair<std::string, std::string>>
pasvFsm(io::Socket &controlSocket)
{
  // Send the command wait for a response.
  const auto pasvCommand(std::string("PASV") + DELIM);
  size_t n = controlSocket.sendString(pasvCommand);
  if (n < pasvCommand.size()) {
    return {};
  }
  const auto maybeResponse = controlSocket.readUntil(DELIM);
  if (!maybeResponse) {
    return {};
  }
  const auto &response = *maybeResponse;

  // Check that we got a positive response. If so, we can parse it for connection information.
  if (response.size() < 3 || response.substr(0, 3) != "227") {
    // The PASV request failed (or was malformed) so there won't be any connection information.
    return {};
  }

  // Regex for finding the connection information. Usually it's also wrapped in perenthesis
  // but according to RFC1123 section 4.1.2.6 we can't rely on that (or even that
  // it's comma-separated, but we will assume so here).
  const std::regex portRegex(
    R"((\d+),(\d+),(\d+),(\d+),(\d+),(\d+))"
  );

  // Look for a match where all the sub-groups also matched.
  std::smatch matches;
  if (std::regex_search(response, matches, portRegex) && matches.size() == 7
        && matches[1].matched && matches[2].matched && matches[3].matched
        && matches[4].matched && matches[5].matched && matches[6].matched
  ) {
    std::string host(matches[1].str() + "." + matches[2].str() + "." + matches[3].str() + "." + matches[4].str());
    // 5th and 6th parts are the upper and lower eight bits of the port number.
    // Convert them to ints, combine them into one value, then convert back to a string.
    std::string port = std::to_string(std::stoi(matches[5]) * 256 + std::stoi(matches[6]));
    return std::make_pair(std::move(host), std::move(port));
  } else {
    // No matches. Can't find the connection information.
    return {};
  }
}

}
