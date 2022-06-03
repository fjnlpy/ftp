#include "ftp/Client.h"

#include <sstream>
#include <fstream>
#include <iostream>
#include <filesystem>
#include <regex>
#include <utility>
#include <string>

#include "util/util.hpp"
#include "fsm/CommandFsm.h"

namespace
{

std::optional<std::string>
parsePwdResponse(const std::string &response)
{
  if (response.substr(0, 3) != "257") {
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
  if (std::regex_search(response, matches, regex) && matches.size() == 2) {
    return matches[1].str();
  } else {
    return {};
  }
}

}

namespace ftp
{

ftp::Client::Client() : controlSocket_()
{ }

bool
Client::connect(const std::string &host)
{
  if (controlSocket_.isOpen()) {
    // Already connected to something, so fail.
    return false;
  }
  bool connected = controlSocket_.connect(host, "ftp");
  // TODO: what if we get told to delay?
  // Receive welcome message from the server (it must send this).
  return connected && controlSocket_.readUntil("\r\n").has_value();
}

bool
Client::login(
  const std::string &username,
  const std::string &password
) {
  // TODO: Check for failures.

  std::ostringstream usernameCommand;
  usernameCommand << "USER " << username << "\r\n";
  controlSocket_.sendString(usernameCommand.str());
  controlSocket_.readUntil("\r\n");

  std::ostringstream passwordCommand;
  passwordCommand << "PASS " << password << "\r\n";
  controlSocket_.sendString(passwordCommand.str());
  return controlSocket_.readUntil("\r\n").has_value();
}

bool
Client::noop()
{
  return fsm::oneStepFsm(controlSocket_, "NOOP");
}

bool
Client::quit()
{
  if (!controlSocket_.isOpen()) {
    // Not connected to anything.
    return false;
  }

  bool hasQuit = fsm::oneStepFsm(controlSocket_, "QUIT");
  if (!hasQuit) {
    // This could mean the message failed to send, or it
    // could be a 500 response from the server (very unlikely,
    // because that should only happen for syntax errors).
    // In either case, just log it and we will shut down
    // the socket anyway (essentially forcing a quit).
    LOG("Error while trying to quit.");
  }
  return controlSocket_.close();
}

bool
Client::stor(const std::string &localSrc, const std::string &serverDest)
{ // TODO: try-catch still needed?
try {
  std::filesystem::path path(localSrc);
  if (!exists(path)) {
    return false;
  }

  // Try and set up data connection.
  auto maybeDataSocket = setupDataConnection();
  if (!maybeDataSocket) {
    return false;
  }
  io::Socket &dataSocket = *maybeDataSocket;

  // Send a store request and see if the server will accept.
  std::ostringstream storCommand;
  storCommand << "STOR " << serverDest << "\r\n";
  controlSocket_.sendString(storCommand.str());
  const auto storResponse = controlSocket_.readUntil("\r\n");
  // a 1xx reply means we can proceed. Anything else means the server won't accept our file.
  if (!storResponse || storResponse->size() == 0 || (*storResponse)[0] != '1') {
    return false;
  }

  // Try and send the file over the data connection.
  bool isSent = dataSocket.sendFile(path);
  LOG("File sent across data socket: isSent=" << (isSent ? "true" : "false"));

  // Close data coonection if not closed, and read the server's response.
  // We need to close explicitly here in case the file-sending failed but the connection remained
  // open. Otherwise, the server won't say anything on the control connection.
  // Note if we do that the server may consider the upload to have succeeded and send a positive
  // response (e.g. if we partially sent the file).
  if (dataSocket.isOpen()) {
    LOG("Data connection still open. Closing.");
    dataSocket.close();
  }
  const auto fileSendResponse = controlSocket_.readUntil("\r\n");
  // TODO: what if something goes wrong on our end after we've sent some bytes, and the server thinks
  // we've sent the whole file and so sends a positive response? Do we then tell it to delete the
  // file?
  return isSent
    && fileSendResponse
    && fileSendResponse->size() > 0
    && (*fileSendResponse)[0] == '2';
} catch (const std::filesystem::filesystem_error &e) {
  return false;
}
}

bool
Client::retr(const std::string &serverSrc, const std::string &localDest)
{
try {
  // Check that the destination is valid.
  const std::filesystem::path destPath(localDest);
  // We won't create directories leading up to the destination file, so if they don't exist then fail.
  // This method can do some funky things if the path contains '.' or '..' (specifically this may not
  // actually be the 'parent' directory -- it may be the same directory or even a child);
  // I think based on this use case it should behave correctly. If not, we may need to canonicalize
  // paths before using them.
  // Note this call can throw implementation-defined exceptions, presumably of
  // type std::filesystem::filesystem_error.
  const std::filesystem::path parentPath = destPath.parent_path();
  const bool isValidDest = exists(parentPath) && is_directory(parentPath) && !exists(destPath);
  if (!isValidDest) {
    LOG("Not a valid destination: " << localDest);
    return false;
  }

  // Try and set up data connection.
  auto maybeDataSocket = setupDataConnection();
  if (!maybeDataSocket) {
    return false;
  }
  io::Socket &dataSocket = *maybeDataSocket;

  // Send RETR request and view the response.
  // This may fail if e.g. we don't permission or the file doesn't exist on the server.
  std::ostringstream retrCommand;
  retrCommand << "RETR " << serverSrc << "\r\n";
  controlSocket_.sendString(retrCommand.str());
  const auto retrResponse = controlSocket_.readUntil("\r\n");
  if (!retrResponse || retrResponse->size() == 0 || (*retrResponse)[0] != '1') {
    return false;
  }

  // Save the data arriving on the data socket until it is closed by the server.
  bool isReceived = dataSocket.retrieveFile(localDest);
  // The connection should still be open here, regardless of whether or not we received an EOF.
  // Close it to make sure the server knows we've finished reading. If the server had sent an EOF
  // then it will probably think the transfer succeeded but we also need to check that there were
  // no errors on our end. Conversely, if the server sends an EOF and everything went well on our
  // end it doesn't necessarily mean the  transfer succeeded as something may have gone wrong
  // on the server's end.
  dataSocket.close();

  // Receive confirmation / error info from server.
  const auto transferResponse = controlSocket_.readUntil("\r\n");
  const bool isServerHappy =
    transferResponse && transferResponse->size() > 0 && (*transferResponse)[0] == '2';

  // Extra sanity check: the file should exist at the destination now.
  const bool isFileAtDestination = exists(destPath);

  return isReceived && isFileAtDestination && isServerHappy;
} catch (const std::filesystem::filesystem_error &e) {
  LOG("Error while retrieving file: error=" << e.what());
  return false;
}
}

std::optional<std::string>
Client::pwd()
{
  controlSocket_.sendString("PWD\r\n");
  const auto response = controlSocket_.readUntil("\r\n");
  if (response) {
    return parsePwdResponse(*response);
  } else {
    return {};
  }
}

bool
Client::cwd(const std::string &newDir)
{
  std::ostringstream cwdCommand;
  cwdCommand << "CWD " << newDir << "\r\n";
  controlSocket_.sendString(cwdCommand.str());
  const auto response = controlSocket_.readUntil("\r\n");
  return response && response->size() > 0 && (*response)[0] == '2';
}

bool
Client::mkd(const std::string &newDir)
{
  // TODO: this should forward the response
  // TODO: fails if it already exists? problem?
  std::ostringstream mkdCommand;
  mkdCommand << "MKD " << newDir << "\r\n";
  controlSocket_.sendString(mkdCommand.str());
  const auto response = controlSocket_.readUntil("\r\n");
  return response && response->size() > 0 && (*response)[0] == '2';
}

std::optional<io::Socket>
Client::setupDataConnection()
{
  // Set correct transfer type.
  // Only the unstructured "image" type is supported.
  // Users can still have structure in their data but they have
  // to manage it themselves.
  if (!fsm::oneStepFsm(controlSocket_, "TYPE I")) {
    return {};
  }

  // Request a passive connection.
  // We use passive connections so that we can initiate the data connection. Otherwise, the server
  // will try to contact us at a port it specifies but that is unlikely to work because most
  // clients won't have that port exposed to the internet.
  const auto maybeConnectionInfo = fsm::pasvFsm(controlSocket_);
  if (!maybeConnectionInfo) {
    // The server didn't give us valid connection information, or some other problem occurred.
    return {};
  }
  const auto &[host, port] = *maybeConnectionInfo;
  LOG("Parsed response: host=" << host << "; port=" << port);
  // TODO: threading considerations?
  io::Socket dataSocket;
  if (!dataSocket.connect(host, port)) {
    return {};
  }
  LOG("Data socket connected.");
  return dataSocket;
}
}
