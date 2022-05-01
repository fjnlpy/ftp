#include "ftp/Client.h"

#include <sstream>

#include "util/util.hpp"

using boost::asio::ip::tcp;

namespace
{
namespace asio
{
bool
connect(
  boost::asio::io_context &ioContext,
  tcp::socket &socket,
  const std::string &url
) {
try {
  auto endpoints = tcp::resolver(ioContext).resolve(url, "ftp");
  boost::asio::connect(socket, std::move(endpoints));
  return true;
} catch (const std::exception &e) {
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
}

namespace ftp
{

ftp::Client::Client(): 
  ioContext_(),
  controlSocket_(ioContext_) 
{ }

bool
Client::connect(const std::string &url)
{
  // TODO: what if we're already connected? maybe fail?
  bool connected = asio::connect(ioContext_, controlSocket_, url);
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

}
