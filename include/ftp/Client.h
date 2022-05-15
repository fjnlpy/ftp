#ifndef FTP_CLIENT_H
#define FTP_CLIENT_H

#include <string>

#include <boost/asio.hpp>

namespace ftp
{

class Client
{
public:
  Client();

  ~Client() =default;

  // We can't have these because Boost's io context doesn't have them.
  // We wouldn't want to copy the socket anyway as it's not worth
  // defining what that means.
  Client(const Client &) =delete;
  Client(Client &&) noexcept =delete;
  Client &operator=(const Client &) =delete;
  Client &operator=(Client &&) noexcept =delete;

  // TODO: Still undecided on when to throw and when to return a result type.
  //  Maybe return bool and have an optional by-ref string that will
  //  be populated with the server's response if passed.
  //  That way users don't need to wrap their code in try-catch
  //  all the time.

  bool connect(const std::string &url);

  bool login(const std::string &username, const std::string &password);

  bool noop();

  bool quit();

  bool stor(const std::string &path);

  std::optional<std::string> pwd();

  bool cwd(const std::string &newDir);

  // TODO: should forward the response, since it contains the server's
  //  understanding of the path for the created dir.
  bool mkd(const std::string &newDir);

private:

  boost::asio::io_context ioContext_;
  boost::asio::ip::tcp::socket controlSocket_;

};

}

#endif
