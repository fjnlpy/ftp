#include <unordered_map>
#include <string>
#include <sstream>
#include <stdexcept>
#include <filesystem>

#include "util/util.hpp"
#include "ftp/Client.h"

#define TEST_ASSERT(e) throwIfFalse(e, __LINE__)

using std::filesystem::path;
using ftp::Client;
using TestFunction = void(*)(Client&, const path&);

namespace {

constexpr auto HOST = "127.0.0.1", USERNAME = "anonymous", PASSWORD = "anonymous";

template <class T>
void
constexpr throwIfFalse(const T &expression, int line)
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
  // assert(is_directory(dir)); // TODO: THIS DOES NOT WORK NOT WORK
  // for (const auto& entry : dir) { 
  //   std::filesystem::remove_all(entry);
  // }
}


auto tests = std::unordered_map<std::string, TestFunction> {
  { "Test unknown host",
  [](Client &client, const path &temp) {
    TEST_ASSERT(!client.connect(USERNAME));
  }
  },

  { "Test successful connection",
  [](Client &client, const path &temp) {
    TEST_ASSERT(client.connect(HOST));
  }
  },

  // TODO: test logging in, once it actually checks for failure
  // TODO: test mkd -- how to delete the new directory afterwards?

  { "Test change and print directory",
  [](Client &client, const path &temp) {
    TEST_ASSERT(client.connect(HOST));
    TEST_ASSERT(client.login(USERNAME, PASSWORD));

    // Should start at root.
    const auto maybeRoot = client.pwd();
    TEST_ASSERT(maybeRoot && *maybeRoot == "/");

    // Change to home.
    TEST_ASSERT(client.cwd("home"));

    // Now should be in /home.
    const auto maybeHome = client.pwd();
    TEST_ASSERT(maybeHome && *maybeHome == "/home");
  }
  },

};
}

int
main(void)
{
  LOG("");
  auto testsExecuted = 0;
  auto testsPassed = 0;

  path tempDir("./scratch/temp");
  if (!exists(tempDir) || !is_empty(tempDir) || !is_directory(tempDir)) {
    LOG("Not proceeding with tests because tempDir either doesn't exist or is not an empty directory.");
    return -1;
  }

  for (const auto &[name, testFunc] : tests) {
    LOG("");
    LOG("===");
    LOG("Running test: " << name);
    LOG("---");
    Client client;
    
    remove_all_inside(tempDir);

    try {
      ++testsExecuted;
      testFunc(client, tempDir);
      // If no exception thrown, test passes.
      ++testsPassed;
      LOG("PASSED");
    } catch (const std::exception &e) {
      LOG("FAILED: " << e.what());
    }

    LOG("===");
  }

  remove_all_inside(tempDir);

  std::stringstream summary;
  summary << "Tests passed: " << testsPassed << "/" << testsExecuted << std::endl;
  LOG("");
  LOG(summary.str());
}
