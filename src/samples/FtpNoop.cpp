#include <iostream>

#include <boost/asio.hpp>

#include "util/util.hpp"

using boost::asio::ip::tcp;

std::string
readFromSocketUntilClosed(tcp::socket &socket)
{
  std::string output;
  boost::system::error_code errorCode;
  while (true) {
    LOG("readFromSocket: iteration begin.");
    std::array<char, 128> buf;
    size_t n = socket.read_some(boost::asio::buffer(buf), errorCode);

    if (errorCode) {
      break;
    } else {
      LOG("readFromSocket: Appending " << n << " bytes of data.");
      output.append(buf.data(), n);
      LOG("readFromSocket: output=" << output);
      LOG("readFromSocket: errorCode=" << errorCode);
    }
  }

  LOG("readFromSocket: break from loop.");
  if (errorCode == boost::asio::error::eof) {
    return output;
  } else {
    throw boost::system::system_error(errorCode);
  }
}

std::string
receiveResponse(tcp::socket &socket)
{
  std::string output;
  // TODO: what if we never receive a reply, or it's malformed? Probably use std::optional.
  const size_t n = boost::asio::read_until(socket, boost::asio::dynamic_buffer(output), "\r\n");
  // Assume there is only one command in the socket. All data after the first occurrance of the delimeter is ignored
  // and may be partially read in a subsequent operation (bad).
  output.erase(output.begin() + n, output.end());
  if (output.size() >= 2) {
    // Remove the \r\n because it's not part of the response.
    output.erase(output.end() - 2, output.end());
  }
  return output;
}

size_t
sendCommand(tcp::socket &socket, const std::string &command)
{
  return boost::asio::write(socket, boost::asio::buffer(command, command.size()));
}

size_t
sendCommand(tcp::socket &socket, std::string &&command)
{
  const size_t size = command.size();
  return boost::asio::write(socket, boost::asio::buffer(std::move(command), size));
}

template <class T>
std::string
logSendAndReceive(const std::string &tag, tcp::socket &socket, T &&command)
{
  LOG("(S) " << tag);
  sendCommand(socket, std::forward<T>(command));
  const std::string response = receiveResponse(socket);
  LOG("(R) " << tag << ": " << response);
  return response;
}

int
main(int argc, char **argv)
{
try {
  constexpr const char *ftpServer("127.0.0.1");
  boost::asio::io_context ioContext;
  auto endpoints = tcp::resolver(ioContext).resolve(ftpServer, "ftp");
  tcp::socket socket(ioContext);

  LOG("(S) connect");
  boost::asio::connect(socket, endpoints);
  LOG("(R) connect: " << receiveResponse(socket));

  logSendAndReceive("username", socket, "USER anonymous\r\n");
  logSendAndReceive("password", socket, "PASS anonymous\r\n");

  const std::string response = logSendAndReceive("noop", socket, "NOOP\r\n");

  switch(response[0]) {
    case '1' :
    case '3' : 
      LOG("Error when receiving noop response :(");break;
    case '2' : 
      LOG("Noop successfully received :)");break;
    case '4' :
    case '5' :
      LOG("Failed to send and receive noop :(");break;
  }

  logSendAndReceive("quit", socket, "QUIT\r\n");

  return 0;
} catch (const std::exception &e) {
  LOG(e.what());
  return 1;
}
}
