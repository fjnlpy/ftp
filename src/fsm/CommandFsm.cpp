#include "fsm/CommandFsm.h"

#include <regex>
#include <cassert>

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
  // so it's safe for callers to check e.g. `response.substr(0,3) == "101"`.
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

// Note: for this Fsm, RFC 959 says "[these commands] expect
// (some may require) 100 series replies" but the only command for this
// Fsm that does not require a 100 series reply is REIN, and we aren't
// exposing REIN, so assume that a 100 series reply is required.
bool
twoStepFsm(
  io::Socket &controlSocket,
  const std::string &command,
  const Callback &onPreliminaryReply
) {

  // Send the command, after which we should be told to wait.
  const auto firstReply = sendCommandAndReceiveReply(controlSocket, command);
  // As explained above, assume we will receive a 1xx reply.
  if (!firstReply || (*firstReply)[0] != '1') {
    return {};
  }

  // Let the caller know we received a 1xx; they may need to
  // do something with a data connection.
  onPreliminaryReply();

  // Server will send the second reply unprompted. For commands
  // that use a data connection, the reply comes when that
  // connection is closed.
  const auto secondReply = controlSocket.readUntil(DELIM);
  return secondReply && secondReply->size() > 0 && (*secondReply)[0] == '2';
}

bool
renameFsm(
  io::Socket &controlSocket,
  const std::string &rnfrArgument,
  const std::string &rntoArgument
) {
  const auto firstReply = sendCommandAndReceiveReply(
    controlSocket,
    std::string("RNFR ") + rnfrArgument + DELIM
  );
  if (!firstReply || (*firstReply)[0] != '3') {
    // Should receive a 3xx reply, which is prompting us to send the RNTO.
    return false;
  }

  const auto secondReply = sendCommandAndReceiveReply(
    controlSocket,
    std::string("RNTO ") + rntoArgument + DELIM
  );
  return secondReply && (*secondReply)[0] == '2';
}

bool
loginFsm(
  io::Socket &controlSocket,
  const std::string &username,
  const std::optional<std::reference_wrapper<const std::string>> &maybePassword,
  const std::optional<std::reference_wrapper<const std::string>> &maybeAccount
) {
  // It's not possible to provide an account without providing a password.
  // The RFC 959 login FSM does not support it.
  assert(!maybeAccount || maybePassword);

  // Send username and check for errors.
  const auto userReply = sendCommandAndReceiveReply(
    controlSocket,
    std::string("USER ") + username + DELIM
  );
  if (!userReply) {
    return false;
  }

  const auto userReplyCode = (*userReply)[0];

  // If no password specified and we get 2xx response, login succeeded
  // with just username. Otherwise, fail because password is required.
  if (!maybePassword) {
    return userReplyCode == '2';
  } 
  
  // If there is a password, send it, even if we got 2xx response.
  // This is against what the RFC FSM says but we want to be consistent
  // with the ACCT case below, and it's unlikely for a server to
  // reject a passworded login if it is willing to accept the same
  // login without a password.

  if (userReplyCode != '2' && userReplyCode != '3') {
    return false;
  }

  const std::string &password = *maybePassword;
  const auto passwordReply = sendCommandAndReceiveReply(
    controlSocket,
    std::string("PASS ") + password + DELIM
  );
  if (!passwordReply) {
    return false;
  }

  const auto passwordReplyCode = (*passwordReply)[0];

  // If no account info and 2xx reply, login succeeded
  // with username and password. Otherwise, fail because
  // account info required
  if (!maybeAccount) {
    return passwordReplyCode == '2';
  }

  // If there is account info, send it, even if the server
  // didn't request it. This is because RFC 959 says
  // the server is permitted to send a certain response
  // at a later point if specific account information is
  // needed at that stage. But we don't parse reply codes
  // in enough detail to be able to detect that response,
  // and I'm not confident that servers would be consistent
  // with their handling of this case (the RFC is not
  // specific about what is expected on either side).
  // Instead, expect that the user will know when
  // they need to provide account information, and
  // send it immediately if it's provided.

  if (passwordReplyCode != '2' && passwordReplyCode != '3') {
    return false;
  }

  const std::string &accountInfo = *maybeAccount;
  const auto acctReply = sendCommandAndReceiveReply(
    controlSocket,
    std::string("ACCT ") + accountInfo + DELIM
  );

  // No more commands to send now, so succeed if the final one succeeded.
  // In this case, login succeeded with username, password and account info.
  return acctReply && (*acctReply)[0] == '2';
}

}
