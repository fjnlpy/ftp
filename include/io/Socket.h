#ifndef IO_SOCKET_H
#define IO_SOCKET_H

#include <utility>
#include <string>
#include <filesystem>
#include <ostream>

#include <boost/asio.hpp>

namespace io {

class Socket {
public:

  Socket();

  ~Socket() =default;

  Socket(const Socket &) =delete;
  Socket(Socket &&) noexcept =default;
  Socket &operator=(const Socket &) =delete;
  Socket &operator=(Socket &&) noexcept =default;

  bool connect(const std::string &host, const std::string &port);

  std::optional<std::string> readUntil(const std::string &delim);

  size_t sendString(const std::string &string);

  bool sendFile(const std::filesystem::path &filePath);

  bool retrieveFile(const std::filesystem::path &filePath);

  bool retrieveToStream(std::ostream &stream);

  bool isOpen();

  bool close();

private:

  // This is static because it can't be moved but ideally Socket should
  // be moveable. It's ok to share this between all sockets because this
  // is a single-threaded application so there can't be any race
  // conditions.
  static inline boost::asio::io_context boostIoContext_{};
  boost::asio::ip::tcp::socket boostSocket_;

  void retrieveToStreamInternal(std::ostream &stream);

};

}

#endif
