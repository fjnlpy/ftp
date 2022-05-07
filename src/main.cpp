#include <cassert>

#include "util/util.hpp"
#include "ftp/Client.h"

int
main(void)
{
  ftp::Client client;
  assert(client.connect("127.0.0.1"));
  assert(client.login("anonymous", "anonymous"));
  //assert(client.noop());
  assert(client.stor("./scratch/file.txt"));
  assert(client.quit());
  return 0;
}
