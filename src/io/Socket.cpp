#include "io/Socket.h"

#include <fstream>
#include <exception>

#include "util/util.hpp"

using boost::asio::ip::tcp;

namespace io {

Socket::Socket() : boostSocket_(boostIoContext_)
{ }

bool
Socket::connect(
  const std::string &host,
  const std::string &port
) {
try {
  auto endpoints = tcp::resolver(boostIoContext_).resolve(host, port);
  boost::asio::connect(boostSocket_, std::move(endpoints));
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

std::optional<std::string>
Socket::readUntil(const std::string &delim)
{
try {
  std::string output;
  // TODO: what if we never receive a reply, or it's malformed? Probably use std::optional.
  //  Note: may be malformed in that there may be characters after the
  //  delimiter, but we're not expecting that as there should only be at
  //  most one pending response from the server on the control socket.
  const size_t n = boost::asio::read_until(
    boostSocket_,
    boost::asio::dynamic_buffer(output),
    delim
  );
  output.erase(output.begin() + n, output.end());
  if (output.size() >= delim.size()) {
    // Remove the delim because it's not part of the response.
    output.erase(output.end() - delim.size(), output.end());
  }
  LOG(output);
  return output;
} catch (const std::exception &e) {
  return {};
}
}

size_t
Socket::sendString(const std::string &string)
{
try {
  return boost::asio::write(boostSocket_, boost::asio::buffer(string, string.size()));
} catch (const std::exception &e) {
  return -1;
}
}


bool
Socket::sendFile(const std::filesystem::path &filePath)
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
    // Assume if anything goes wrong an exception will be thrown i.e. no need
    // to check return value.
    boost::asio::write(boostSocket_, boost::asio::buffer(buf, fileStream.gcount()));
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
      boost::asio::write(boostSocket_, boost::asio::buffer(buf, fileStream.gcount()));
    }
    return true;
  }
} catch (const std::exception &e) {
  LOG("Error while sending file. error=" << e.what());
  return false;
}
}


bool
Socket::retrieveFile(const std::filesystem::path &filePath)
{
  assert(!exists(filePath));
try {

  // Create a binary stream for saving the data. The destination file shouldn't exist but
  // this will create it for us (and there won't be any errors as long as it's successful).
  std::ofstream fileStream(filePath, std::ios::binary);
  if (!fileStream) {
    LOG("Could not create file stream: filePath=" << filePath);
    return false;
  }

  // Read and save the data arriving on the data socket.
  constexpr size_t chunkSize = 1024;
  std::array<char, chunkSize> buf;
  boost::system::error_code errorCode;
  // Read until the server closes the socket -- which indicates that the transfer has
  // finished (successfully or otherwise).
  while (!errorCode) { // TODO: what if adversarial server doesn't reply? Need a timeout.
    size_t n = boostSocket_.read_some(boost::asio::buffer(buf), errorCode);
    fileStream.write(buf.data(), n);
  }

  if (errorCode == boost::asio::error::eof) {
    // Server closed the socket on their end. There may still be an error on the server's side
    // but from the perspective of this method, the transfer succeeded.
    return true;
  } else {
    // Something went wrong on our end, so the transfer should definitely be considered a failure.
    LOG("Error while receiving file: error=" << boost::system::system_error(errorCode).what());
    return false;
  }

} catch (const std::exception &e) {
  // TODO: can there be any exceptions here?
  LOG("Error while retreiving file. error=" << e.what());
  return false;
}
}

bool
Socket::isOpen()
{
  return boostSocket_.is_open();
}

bool
Socket::close()
{
try {
  boostSocket_.shutdown(tcp::socket::shutdown_both);
  boostSocket_.close();
  return true;
} catch(const std::exception &e) {
  return false;
}
}

}
