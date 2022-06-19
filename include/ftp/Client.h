#ifndef FTP_CLIENT_H
#define FTP_CLIENT_H

#include <string>
#include <functional>

#include "io/Socket.h"

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

  bool stor(const std::string &localSrc, const std::string &serverDest);

  bool appe(const std::string &localSrc, const std::string &serverDest);

  bool retr(const std::string &serverSrc, const std::string &localDest);

  std::optional<std::string> pwd();

  bool cwd(const std::string &newDir);

  std::optional<std::string> mkd(const std::string &newDir);

  bool dele(const std::string &fileToDelete);

  bool rmd(const std::string &dirToDelete);

  std::optional<std::string> list(const std::string &dirToList);

  std::optional<std::string> list();

private:

  io::Socket controlSocket_;

  std::optional<io::Socket> setupDataConnection();

  std::optional<std::string> list(const std::optional<std::reference_wrapper<const std::string>>&);

  bool storOrAppe(const std::string &localSrc, const std::string &serverDest, bool isAppendOperation);
};

}

#endif
