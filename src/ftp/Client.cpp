#include "ftp/Client.h"

#include <sstream>
#include <filesystem>
#include <regex>

#include "util/util.hpp"

using boost::asio::ip::tcp;
using namespace std::filesystem;

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
    const std::string host(matches[1].str() + "." + matches[2].str() + "." + matches[3].str() + "." + matches[4].str());
    // 5th and 6th parts are the upper and lower eight bits of the port number.
    // Convert them to ints, combine them into one value, then convert back to a string.
    const std::string port = std::to_string(std::stoi(matches[5]) * 256 + std::stoi(matches[6]));
    return std::make_pair(std::move(host), std::move(port));
  } else {
    // No matches. Can't find the port information.
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
    LOG("Welcome message: " << asio::receiveResponse(controlSocket_));
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
Client::stor(const std::string &pathToFile)
{
try {
  path path(pathToFile);
  if (exists(path)) {

    // Set correct transfer type and request a passive connection.
    // We use passive connections so that we don't need to set up any
    // port forwarding to let the server open connections to us.
    asio::sendCommand(controlSocket_, "TYPE I\r\n");
    LOG(asio::receiveResponse(controlSocket_));
    asio::sendCommand(controlSocket_, "PASV\r\n");
    const std::string response = asio::receiveResponse(controlSocket_);
    LOG(response);

    // Determine which port to use by parsing the response, and open a data
    // connection to that port.
    auto parsedResponse = parsePasv(response);
    if (parsedResponse) {
      LOG("PASV port: " << *parsedResponse);
    } else {
      LOG("Couldn't parse response :(");
    }

    // TODO: open the file as a stream

    // TODO: send the file with asio

    // TODO: what if something goes wrong?

    return true; // TODO:
  } else {
    return false;
  }
} catch (const filesystem_error &e) {
  return false;
}
}

}
