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
{
  return storOrAppe(localSrc, serverDest, false);
}

bool
Client::appe(const std::string &localSrc, const std::string &serverDest)
{
  return storOrAppe(localSrc, serverDest, true);
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
    return false;
  }

  // Try and set up data connection.
  auto maybeDataSocket = setupDataConnection();
  if (!maybeDataSocket) {
    return false;
  }
  io::Socket &dataSocket = *maybeDataSocket;

  // This lambda is called if/when we receive a 1xx reply from the server.
  bool isReceived = false;
  const auto onPreliminaryReply = [&dataSocket, &destPath, &isReceived]() {
    // Save the data arriving on the data socket until it is closed by the server.
    isReceived = dataSocket.retrieveFile(destPath);
    // The connection should still be open here, regardless of whether or not we received an EOF.
    // Close it to make sure the server knows we've finished reading. If the server had sent an EOF
    // then it will probably think the transfer succeeded but we also need to check that there were
    // no errors on our end. Conversely, if the server sends an EOF and everything went well on our
    // end it doesn't necessarily mean the  transfer succeeded as something may have gone wrong
    // on the server's end.
    dataSocket.close();
  };

  // Try to retrieve the file from the server.
  // This may fail if e.g. we don't permission or the file doesn't exist on the server.
  const bool isServerHappy = fsm::twoStepFsm(
    controlSocket_,
    std::string("RETR ") + serverSrc,
    onPreliminaryReply
  );

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
  return fsm::directoryFsm(controlSocket_, {});
}

bool
Client::cwd(const std::string &newDir)
{
  return fsm::oneStepFsm(controlSocket_, std::string("CWD ") + newDir);
}

std::optional<std::string>
Client::mkd(const std::string &newDir)
{
  return fsm::directoryFsm(controlSocket_, newDir);
}

bool
Client::dele(const std::string &fileToDelete)
{
  return fsm::oneStepFsm(controlSocket_, std::string("DELE ") + fileToDelete);
}

bool
Client::rmd(const std::string &dirToDelete)
{
  return fsm::oneStepFsm(controlSocket_, std::string("RMD ") + dirToDelete);
}

std::optional<std::string>
Client::list(const std::string &dirToList)
{
  return list(std::make_optional(dirToList));
}

std::optional<std::string>
Client::list()
{
  return list(std::nullopt);
}

std::optional<std::string>
Client::list(const std::optional<std::reference_wrapper<const std::string>> &maybeDirToList)
{
  // Assume the output fits sensibly into a string. If there were many directories in the
  // output, this would not be reasonable. Ideally, we should truncate the output if it's
  // above a certain size.
  std::optional<std::string> maybeListOutput(std::nullopt);

  // Construct command depending on whether user specified a directory explicitly.
  // If not, the server should assume they want to list the current directory.
  std::string command("LIST ");
  if (maybeDirToList) {
    command.append(*maybeDirToList);
  }

  // Set up data connection. Note the original ftp says we should use the ASCII transfer type
  // for list commands but we will assume the server is robust to any transfer type (specifically
  // to image type, which we use everywhere else). It shouldn't matter to the server or to us
  // because we just print the data we receive as a string; we don't need to interpret it.
  auto maybeDataSocket = setupDataConnection();
  if (!maybeDataSocket) {
    return {};
  }
  io::Socket &dataSocket = *maybeDataSocket;

  const auto onPreliminaryReply = [&maybeListOutput, &dataSocket]() {
    std::stringstream outputStream;

    bool isSuccess = dataSocket.retrieveToStream(outputStream);
    if (isSuccess) {
      // It would be nice to do std::move(outputStream).str() here
      // to move assign the output from the string stream. Unfortunately,
      // that's not possible until C++20, which means we have to make
      // a copy of the (potentially large) string that is placed in
      // the stringstream by the socket operation.
      // I don't know a workaround for this that works with output streams.
      maybeListOutput = outputStream.str();
    }

    if (dataSocket.isOpen()) {
      dataSocket.close();
    }
  };

  // Send request and return response.
  fsm::twoStepFsm(controlSocket_, command, onPreliminaryReply);

  return maybeListOutput;
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

bool
Client::storOrAppe(const std::string &localSrc, const std::string &serverDest, bool isAppendOperation)
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

  // Lambda for sending the file; called if/when the server responds
  // with a 1xx.
  bool isSent = false;
  const auto onPreliminaryReply = [&dataSocket, &path, &isSent]() {
    // Try and send the file over the data connection.
    isSent = dataSocket.sendFile(path);

    // Close data connection.
    // The server should close this on its end but the io::Socket will still be
    // open until it's explicitly closed on our end.
    if (dataSocket.isOpen()) {
      dataSocket.close();
    }
  };

  // Send the request, using either append mode or overwrite mode depending on
  // the argument.
  const bool isServerHappy = fsm::twoStepFsm(
    controlSocket_,
    std::string(isAppendOperation ? "APPE " : "STOR ") + serverDest,
    onPreliminaryReply
  );

  // TODO: what if something goes wrong on our end after we've sent some bytes, and the server thinks
  // we've sent the whole file and so sends a positive response? Do we then tell it to delete the
  // file?
  // Succeed if nothing went wrong on our end and the server gave a positive response.
  return isSent && isServerHappy;
} catch (const std::filesystem::filesystem_error &e) {
  return false;
}
}

}
