#include "fsm/CommandFsm.h"

#include <regex>

namespace {

constexpr auto DELIM = "\r\n";

std::optional<std::string>
sendCommandAndReceiveReply(io::Socket &controlSocket, const std::string &command)
{
  const auto commandWithDelim(command + DELIM);
  size_t n = controlSocket.sendString(commandWithDelim);
  if (n < commandWithDelim.size()) {
    return {};
  }

  const auto maybeResponse = controlSocket.readUntil(DELIM);
  if (!maybeResponse || maybeResponse->size() < 3) {
    return {};
  }

  // Note that the response must be at least three characters long,
  // so it's safe for callees to check e.g. `response.substr(0,3) == "101"`.
  return *maybeResponse;
}

}

namespace fsm {

bool
oneStepFsm(
  io::Socket &controlSocket,
  const std::string &command
) {
  if (auto reply = sendCommandAndReceiveReply(controlSocket, command)) {
    return (*reply)[0] == '2';
  } else {
    return {};
  }
}

std::optional<std::pair<std::string, std::string>>
pasvFsm(io::Socket &controlSocket)
{
  // Send the command wait for a response.
  const auto maybeResponse = sendCommandAndReceiveReply(controlSocket, std::string("PASV") + DELIM);
  if (!maybeResponse) {
    return {};
  }
  const auto &response = *maybeResponse;

  // Check that we got a positive response. If so, we can parse it for connection information.
  if (response.substr(0, 3) != "227") {
    // The PASV request failed so there won't be any connection information.
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

std::optional<std::string>
directoryFsm(io::Socket &controlSocket, const std::optional<std::string> &path)
{
  const std::string command = (path ? (std::string("MKD ") + *path) : std::string("PWD")) + DELIM;
  const auto response = sendCommandAndReceiveReply(controlSocket, command);
  if (!response) {
    return {};
  }

  if (response->substr(0, 3) != "257") {
    // Response indicates failure.
    return {};
  }

  // The response should be of the form `257<sp>"<dir>"[<other stuff>]\r\n`
  // Use regex to extract the part between the quotes.
  // TODO: how to handle "quote doubling" convention, or other possible conventions for nested
  //   qoutations (e.g. escape characters) without accidentally grabbing too much?

  // Note we match the longest substring which is inside quotes. That should handle any nested
  // quoting but may cause problems if there are quotes elsewhere in the message.
  const std::regex regex(
    R"(257 \"(.*)\".*)"
  );

  std::smatch matches;
  if (std::regex_search(*response, matches, regex) && matches.size() == 2) {
    return matches[1].str();
  } else {
    // No matches so can't return the path to the directory. Note that we are returning
    // a null optional here but we did successfully create the directory. In other words,
    // a null response doesn't imply that the operation failed.
    return {};
  }
}

}
