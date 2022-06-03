#include <unordered_map>
#include <string>
#include <sstream>
#include <stdexcept>
#include <filesystem>

#include "util/util.hpp"
#include "ftp/Client.h"

#define TEST_ASSERT(e) throwIfFalse(e, __LINE__)

using std::filesystem::path;
using std::filesystem::directory_iterator;
using ftp::Client;
using TestFunction = void(*)(Client&, const path&, const path&);

namespace {

constexpr auto HOST = "127.0.0.1", USERNAME = "anonymous", PASSWORD = "anonymous";

template <class T>
void
throwIfFalse(const T &expression, int line)
{
  if (!expression) {
    std::stringstream msg;
    msg << "Assertion triggered at line " << line << std::endl;
    throw std::runtime_error(msg.str());
  }
}

void
remove_all_inside(const path &dir)
{
  assert(is_directory(dir));
  for (const auto& entry : directory_iterator(dir)) {
    std::filesystem::remove_all(entry);
  }
}


auto tests = std::unordered_map<std::string, TestFunction> {
  { "Test unknown host",
  [](Client &client, const path &, const path &) {
    TEST_ASSERT(!client.connect(USERNAME));
  }
  },

  { "Test successful connection",
  [](Client &client, const path &, const path &) {
    TEST_ASSERT(client.connect(HOST));
  }
  },

  // TODO: test logging in, once it actually checks for failure
  // TODO: test mkd -- how to delete the new directory afterwards?

  { "Test change and print directory",
  [](Client &client, const path &, const path &) {
    TEST_ASSERT(client.connect(HOST));
    TEST_ASSERT(client.login(USERNAME, PASSWORD));

    // Should start at root.
    const auto maybeRoot = client.pwd();
    TEST_ASSERT(maybeRoot && *maybeRoot == "/");

    // Change to temp.
    TEST_ASSERT(client.cwd("temp"));

    // Now should be in /temp.
    const auto maybeTemp = client.pwd();
    TEST_ASSERT(maybeTemp && *maybeTemp == "/temp");
  }
  },

  { "Test upload big file",
  [](Client &client, const path &, const path &serverTemp) {
    TEST_ASSERT(client.connect(HOST));
    TEST_ASSERT(client.login(USERNAME, PASSWORD));

    TEST_ASSERT(client.stor("scratch/files/bigfile.txt", "temp/uploadedfile.txt"));

    const auto uploadedFile(serverTemp/"uploadedfile.txt");
    // bigfile contains 2049 bytes; uploadedFile should be the same size.
    TEST_ASSERT(file_size(uploadedFile) == 2049);
  }
  }

};
}

int
main(void)
{
  LOG("");
  auto testsExecuted = 0;
  auto testsPassed = 0;

  path localTemp("./scratch/temp");
  if (!exists(localTemp) || !is_empty(localTemp) || !is_directory(localTemp)) {
    LOG("Not proceeding with tests because local temp dir either doesn't exist or is not an empty directory.");
    return -1;
  }
  path serverTemp("./vsftpd/anon/temp");
  if (!exists(serverTemp) || !is_empty(serverTemp) || !is_directory(serverTemp)) {
    LOG("Not proceeding with tests because server temp dir either doesn't exist or is not an empty directory.");
    return -1;
  }

  for (const auto &[name, testFunc] : tests) {
    LOG("");
    LOG("===");
    LOG("Running test: " << name);
    LOG("---");
    Client client;
    
    remove_all_inside(localTemp);
    remove_all_inside(serverTemp);

    try {
      ++testsExecuted;
      testFunc(client, localTemp, serverTemp);
      // If no exception thrown, test passes.
      ++testsPassed;
      LOG("PASSED");
    } catch (const std::exception &e) {
      LOG("FAILED: " << e.what());
    }

    LOG("===");
  }

  remove_all_inside(localTemp);
  remove_all_inside(serverTemp);

  std::stringstream summary;
  summary << "Tests passed: " << testsPassed << "/" << testsExecuted << std::endl;
  LOG("");
  LOG(summary.str());
}
