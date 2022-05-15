#include "ftp/Client.h"

#include <sstream>
#include <fstream>
#include <iostream>
#include <filesystem>
#include <regex>
#include <utility>
#include <string>

#include "util/util.hpp"

#define DEBUG

using boost::asio::ip::tcp;

namespace
{
namespace asio
{
bool
connect(
  boost::asio::io_context &ioContext,
  tcp::socket &socket,
  const std::string &host,
  const std::string &port
) {
try {
  auto endpoints = tcp::resolver(ioContext).resolve(host, port);
  boost::asio::connect(socket, std::move(endpoints));
  return true;
} catch (const std::exception &e) {
  LOG(
    "Could not make connection. host=" << host
    << "; port=" << port
    << "; error=" << e.what()
  );
  return false;
}
}

std::string
receiveResponse(tcp::socket &socket)
{
try {
  std::string output;
  // TODO: what if we never receive a reply, or it's malformed? Probably use std::optional.
  //  Note: may be malformed in that there may be characters after the
  //  delimiter, but we're not expecting that as there should only be at
  //  most one pending response from the server on the control socket.
  const size_t n = boost::asio::read_until(socket, boost::asio::dynamic_buffer(output), "\r\n");
  output.erase(output.begin() + n, output.end());
  if (output.size() >= 2) {
    // Remove the \r\n because it's not part of the response.
    output.erase(output.end() - 2, output.end());
  }
  return output;
} catch (const std::exception &e) {
  // TODO:
  exit(1);
}
}

size_t
sendCommand(tcp::socket &socket, const std::string &command)
{
try {
  return boost::asio::write(socket, boost::asio::buffer(command, command.size()));
} catch (const std::exception &e) {
  return 0;
}
}

bool
sendFile(tcp::socket &socket, const std::filesystem::path &filePath)
{
  assert(exists(filePath) && (is_regular_file(filePath) || is_character_file(filePath)));
try
{
  std::ifstream fileStream(filePath, std::ios::binary);
  if (!fileStream) {
    LOG("Could not open filestream; path=" << filePath);
    return false;
  }

  // Reset gcount before the loop starts.
  fileStream.peek();

  constexpr size_t chunkSize = 1024;
  std::array<char, chunkSize> buf;

  // Send 1KB chunks until the stream fails.
  LOG("Sending file: chunkSize=" << chunkSize);
  while (fileStream.read(buf.data(), chunkSize)) {
    #ifdef DEBUG
    LOG("===NEW CHUNK===");
    for (size_t i = 0; i < chunkSize; ++i) {
      std::cerr << buf[i];
    }
    std::cerr << std::endl;
    LOG("===END CHUNK===");
    #endif
    // Assume if anything goes wrong an exception will be thrown i.e. no need
    // to check return value.
    boost::asio::write(socket, boost::asio::buffer(buf, fileStream.gcount()));
  }

  if (!fileStream.eof()) {
    // The stream didn't fail because of reaching the end of the file, so
    // something went wrong.
    // TODO: how do we tell the server that file is useless / not complete?
    LOG("File stream did not complete correctly. Stopping.");
    return false;
  } else {
    LOG("Reached EOF.");
    // We reached eof. Send any data that was read in the final read operation.
    if (fileStream.gcount()) {
      LOG("Sending extra data: gcount=" << fileStream.gcount());

      #ifdef DEBUG // TODO: avoid repeating this debug printing code; maybe define an ostream for array<char,size>?
      LOG("===NEW CHUNK===");
      for (size_t i = 0; i < fileStream.gcount(); ++i) {
        std::cerr << buf[i];
      }
      std::cerr << std::endl;
      LOG("===END CHUNK===");
      #endif

      boost::asio::write(socket, boost::asio::buffer(buf, fileStream.gcount()));
    }
    return true;
  }
} catch (const std::exception &e) {
  LOG("Error while sending file. error=" << e.what());
  return false;
}
}

bool
closeSocket(tcp::socket &socket)
{
try {
  socket.shutdown(socket.shutdown_both);
  socket.close();
  return true;
} catch(const std::exception &e) {
  return false;
}
}

}

std::optional<std::pair<std::string, std::string>>
parsePasv(const std::string &pasvResponse)
{
  if (pasvResponse.substr(0, 3) != "227") {
    // The PASV request failed (or is malformed) so we can't
    // extract the port information.
    return {};
  }

  // Regex for finding the port information. Usually it's also wrapped in perenthesis
  // but according to RFC1123 section 4.1.2.6 we can't rely on that (or even that
  // it's comma-separated, but we will assume so here).
  const std::regex portRegex(
    R"((\d+),(\d+),(\d+),(\d+),(\d+),(\d+))"
  );

  // Look for a match where all the sub-groups also matched.
  std::smatch matches;
  if (std::regex_search(pasvResponse, matches, portRegex) && matches.size() == 7
        && matches[1].matched && matches[2].matched && matches[3].matched
        && matches[4].matched && matches[5].matched && matches[6].matched
  ) {
    std::string host(matches[1].str() + "." + matches[2].str() + "." + matches[3].str() + "." + matches[4].str());
    // 5th and 6th parts are the upper and lower eight bits of the port number.
    // Convert them to ints, combine them into one value, then convert back to a string.
    std::string port = std::to_string(std::stoi(matches[5]) * 256 + std::stoi(matches[6]));
    return std::make_pair(std::move(host), std::move(port));
  } else {
    // No matches. Can't find the port information.
    return {};
  }
}

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

ftp::Client::Client(): 
  ioContext_(),
  controlSocket_(ioContext_) 
{ }

bool
Client::connect(const std::string &host)
{
  // TODO: what if we're already connected? maybe fail?
  bool connected = asio::connect(ioContext_, controlSocket_, host, "ftp");
  if (connected) {
    // TODO: what if we get told to delay?
    // Receive the welcome message from the server.
    // Servers must send us this.
    LOG(asio::receiveResponse(controlSocket_));
  }
  return connected;
}

bool
Client::login(
  const std::string &username,
  const std::string &password
) {
  std::ostringstream usernameCommand;
  usernameCommand << "USER " << username << "\r\n";
  asio::sendCommand(controlSocket_, usernameCommand.str());
  LOG(asio::receiveResponse(controlSocket_));

  std::ostringstream passwordCommand;
  passwordCommand << "PASS " << password << "\r\n";
  asio::sendCommand(controlSocket_, passwordCommand.str());
  LOG(asio::receiveResponse(controlSocket_));
  // TODO: Check for failure.
  return true;
}

bool
Client::noop()
{
  bool succeeded = asio::sendCommand(controlSocket_, "NOOP\r\n") > 0;
  if (succeeded) {
    LOG(asio::receiveResponse(controlSocket_));
  }
  return succeeded;
}

bool
Client::quit()
{
  // TODO: what if the socket isn't connected to anything?
  asio::sendCommand(controlSocket_, "QUIT\r\n");
  LOG(asio::receiveResponse(controlSocket_));
  asio::closeSocket(controlSocket_);
  // TODO: handle errors.
  return true;
}

bool
Client::stor(const std::string &filePath, const std::string &destination)
{ // TODO: try-catch still needed?
try {
  std::filesystem::path path(filePath);
  if (!exists(path)) {
    return false;
  }

  // Try and set up data connection.
  auto maybeDataSocket = setupDataConnection();
  if (!maybeDataSocket) {
    return false;
  }
  tcp::socket &dataSocket = *maybeDataSocket;

  // Send a store request and see if the server will accept.
  std::ostringstream storCommand;
  storCommand << "STOR " << destination << "\r\n";
  asio::sendCommand(controlSocket_, storCommand.str());
  const std::string storResponse = asio::receiveResponse(controlSocket_);
  LOG(storResponse);
  // a 1xx reply means we can proceed. Anything else means the server won't accept our file.
  if (storResponse.size() == 0 || storResponse[0] != '1') {
    return false;
  }

  // Try and send the file over the data connection.
  // Note we don't check the return type because we will use the server's response to
  // decide if the upload succeeded.
  bool isSent = asio::sendFile(dataSocket, path);
  LOG("File sent across data socket: isSent=" << (isSent ? "true" : "false"));

  // Close data coonection if not closed, and read the server's response.
  // We need to close explicitly here in case the file-sending failed but the connection remained
  // open. Otherwise, the server won't say anything on the control connection.
  // Note if we do that the server may consider the upload to have succeeded and send a positive
  // response (e.g. if we partially sent the file).
  if (dataSocket.is_open()) {
    LOG("Data connection still open. Closing.");
    asio::closeSocket(dataSocket);
  }
  const std::string fileSendResponse = asio::receiveResponse(controlSocket_);
  // TODO: what i something goes wrong on our end after we've sent some bytes, and the server thinks
  // we've sent the whole file and so sends a positive response? Do we then tell it to delete the
  // file?
  LOG(fileSendResponse);
  return isSent && fileSendResponse.size() > 0 && fileSendResponse[0] == '2';
} catch (const std::filesystem::filesystem_error &e) {
  return false;
}
}

std::optional<std::string>
Client::pwd()
{
  asio::sendCommand(controlSocket_, "PWD\r\n");
  const std::string response = asio::receiveResponse(controlSocket_);
  LOG(response);
  return parsePwdResponse(response);
}

bool
Client::cwd(const std::string &newDir)
{
  std::ostringstream cwdCommand;
  cwdCommand << "CWD " << newDir << "\r\n";
  asio::sendCommand(controlSocket_, cwdCommand.str());
  const std::string response = asio::receiveResponse(controlSocket_);
  LOG(response);
  return response.size() > 0 && response[0] == '2';
}

bool
Client::mkd(const std::string &newDir)
{
  // TODO: this should forward the response
  // TODO: fails if it already exists? problem?
  std::ostringstream mkdCommand;
  mkdCommand << "MKD " << newDir << "\r\n";
  asio::sendCommand(controlSocket_, mkdCommand.str());
  const std::string response = asio::receiveResponse(controlSocket_);
  LOG(response);
  return response.size() > 0 && response[0] == '2';
}

std::optional<tcp::socket>
Client::setupDataConnection()
{
  // Set correct transfer type and request a passive connection.
  // We use passive connections so that we don't need to set up any
  // port forwarding to let the server open connections to us.
  asio::sendCommand(controlSocket_, "TYPE I\r\n");
  LOG(asio::receiveResponse(controlSocket_));
  asio::sendCommand(controlSocket_, "PASV\r\n");
  const std::string pasvResponse = asio::receiveResponse(controlSocket_);
  LOG(pasvResponse);

  // Determine which port to use by parsing the response, and open a data
  // connection to that port.
  auto parsedResponse = parsePasv(pasvResponse);
  if (!parsedResponse) {
    return {};
  }
  LOG("Parsed response: host=" << parsedResponse->first << "; port=" << parsedResponse->second);
  // TODO: threading considerations?
  tcp::socket dataSocket(ioContext_);
  if (!asio::connect(ioContext_, dataSocket, parsedResponse->first, parsedResponse->second)) {
    return {};
  }
  LOG("Data socket connected.");
  return dataSocket;
}
}
