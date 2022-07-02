#include <unordered_map>
#include <string>
#include <sstream>
#include <stdexcept>
#include <filesystem>
#include <fstream>
#include <vector>
#include <algorithm>

#include "util/util.hpp"
#include "ftp/Client.h"

#define TEST_ASSERT(e) throwIfFalse(e, __LINE__)

namespace fs = std::filesystem;
using fs::path;

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
  for (const auto& entry : fs::directory_iterator(dir)) {
    fs::remove_all(entry);
  }
}

void
assertConnectAndLogin(Client &client)
{
  TEST_ASSERT(client.connect(HOST));
  TEST_ASSERT(client.login(USERNAME, PASSWORD));
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

  { "Test change and print directory",
  [](Client &client, const path &, const path &) {
    assertConnectAndLogin(client);

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

  { "Test CWD not logged in",
  [](Client &client, const path &, const path &) {
    // Don't log in; should be unable to change directory.

    TEST_ASSERT(!client.cwd("temp"));
  }
  },

  { "Test CWD invalid directory",
  [](Client &client, const path &, const path &) {
    assertConnectAndLogin(client);

    TEST_ASSERT(!client.cwd("NotARealDirectory"));
  }
  },

  { "Test upload 2049 byte file",
  [](Client &client, const path &, const path &serverTemp) {
    // 2049 because that's one larger than a multiple of the block size we use.
    // Make sure the extra byte is still sent.
    assertConnectAndLogin(client);

    TEST_ASSERT(client.stor("scratch/files/bigfile-2049.txt", "temp/uploadedfile.txt"));

    const auto uploadedFile(serverTemp/"uploadedfile.txt");
    // bigfile contains 2049 bytes; uploadedFile should be the same size.
    TEST_ASSERT(exists(uploadedFile) && file_size(uploadedFile) == 2049);
  }
  },

  { "Test upload 2048 byte file",
  [](Client &client, const path &, const path &serverTemp) {
    // 2048 because that's equal to the block size we use.
    // Make sure the loop still terminates with a zero byte-read eof.
    assertConnectAndLogin(client);

    TEST_ASSERT(client.stor("scratch/files/bigfile-2048.txt", "temp/uploadedfile.txt"));

    const auto uploadedFile(serverTemp/"uploadedfile.txt");
    // bigfile contains 2048 bytes; uploadedFile should be the same size.
    TEST_ASSERT(exists(uploadedFile) && file_size(uploadedFile) == 2048);
  }
  },

  { "Test download big file",
  [](Client &client, const path &localTemp, const path &) {
    assertConnectAndLogin(client);

    const auto downloadedFile(localTemp/"downloadedfile.txt");
    TEST_ASSERT(client.retr("files/bigfile.txt", downloadedFile));

    TEST_ASSERT(exists(downloadedFile) && file_size(downloadedFile) == 2050);
  }
  },

  // { "Test download really big file",
  // [](Client &client, const path &localTemp, const path &) {
  //   assertConnectAndLogin(client);

  //   const auto downloadedFile(localTemp/"downloadedfile.txt");
  //   TEST_ASSERT(client.retr("files/reallybigfile.txt", downloadedFile));

  //   TEST_ASSERT(exists(downloadedFile) && file_size(downloadedFile) == 1000 * 1024 * 1024);
  // }
  // },

  { "Test make directory",
  [](Client &client, const path &, const path &serverTemp) {
    assertConnectAndLogin(client);

    // This should fail because newdir doesn't exist.
    TEST_ASSERT(!client.cwd("temp/newdir"));

    // Now create newdir.
    const auto response = client.mkd("temp/newdir");
    // Server should tell us its path to the directory.
    TEST_ASSERT(response && *response == "/temp/newdir");

    // This should now succeed.
    TEST_ASSERT(client.cwd("temp/newdir"));

    // And newdir should exist on the server.
    const auto newDir(serverTemp/"newdir");
    TEST_ASSERT(exists(newDir) && is_directory(newDir));
  }
  },

  { "Test MKD directory already exists",
  [](Client &client, const path &, const path &) {
    assertConnectAndLogin(client);

    // Temp should already exist because the test infrastructure creates it.
    TEST_ASSERT(!client.mkd("temp"));
  }
  },

  {"Test Noop",
  [](Client &client, const path &, const path &) {
    assertConnectAndLogin(client);

    TEST_ASSERT(client.noop());
  }
  },

  { "Test quit",
  [](Client &client, const path &, const path &) {
    assertConnectAndLogin(client);

    TEST_ASSERT(client.quit());
  }
  },

  { "Test quit while not logged in",
  [](Client &client, const path &, const path &) {
    // Don't log in; quit should fail if not logged in.

    TEST_ASSERT(!client.quit());
  }
  },

  { "Test delete",
  [](Client &client, const path &, const path &serverTemp) {
    assertConnectAndLogin(client);

    const auto newFile(serverTemp/"newfile");

    // This should create a new file.
    TEST_ASSERT(std::ofstream(newFile));
    TEST_ASSERT(exists(newFile));

    TEST_ASSERT(client.dele("temp/newfile"));

    TEST_ASSERT(!exists(newFile));
  }
  },

  { "Test upload and delete",
  [](Client &client, const path &, const path &) {
    assertConnectAndLogin(client);

    TEST_ASSERT(client.stor("scratch/files/file.txt", "temp/file.txt"));

    // Checks there's no specific issues with deleting a file that was created
    // via upload.
    TEST_ASSERT(client.dele("temp/file.txt"));
  }
  },

  { "Test can't use DELE on a directory",
  [](Client &client, const path &, const path &serverTemp) {
    assertConnectAndLogin(client);

    TEST_ASSERT(create_directory(serverTemp/"newDir"));

    // We shouldn't be allowed to delete directories with this command.
    TEST_ASSERT(!client.dele("temp/newDir"));

  }
  },

  { "Test rmd",
  [](Client &client, const path &, const path &serverTemp) {
    assertConnectAndLogin(client);

    const auto newDir(serverTemp/"newdir");

    TEST_ASSERT(fs::create_directory(newDir));

    TEST_ASSERT(client.rmd("temp/newdir"));

    TEST_ASSERT(!exists(newDir));
  }
  },

  { "Test mkd and rmd",
  [](Client &client, const path &, const path &) {
    assertConnectAndLogin(client);

    TEST_ASSERT(client.mkd("temp/newdir"));

    // Checks there's no specific issues with removing a directory that
    // has been created via mkd.
    TEST_ASSERT(client.rmd("temp/newdir"));
  }
  },

  { "Test can't use rmd on a file",
  [](Client &client, const path &, const path &serverTemp) {
    assertConnectAndLogin(client);

    const auto newFile(serverTemp/"newfile.txt");

    TEST_ASSERT(std::ofstream(newFile));
    TEST_ASSERT(exists(newFile));

    // Shouldn't work because we can only remove directories with rmd.
    TEST_ASSERT(!client.rmd("temp/newfile.txt"));
  }
  },

  { "Test rmd directory which contains a file",
  [](Client &client, const path &, const path &serverTemp) {
    assertConnectAndLogin(client);

    const auto newDir(serverTemp/"newdir");
    const auto newFile(newDir/"newfile.txt");

    TEST_ASSERT(fs::create_directory(newDir));
    TEST_ASSERT(std::ofstream(newFile));
    TEST_ASSERT(exists(newFile));

    // This is not allowed for the server I am using. Apparently some servers
    // do support this.
    TEST_ASSERT(!client.rmd("temp/newdir/newfile.txt"));
  }
  },

  { "Test list empty directory",
  [](Client &client, const path &, const path &) {
    assertConnectAndLogin(client);

    // The server temp dir should be empty already.
    client.cwd("temp");

    const auto maybeList = client.list();
    TEST_ASSERT(maybeList);
    TEST_ASSERT(*maybeList == "");
  }
  },

  { "Test list non-empty directory via no args",
  [](Client &client, const path &, const path &) {
    assertConnectAndLogin(client);

    // The files directory should be non-empty.
    client.cwd("files");

    const auto maybeList = client.list();
    TEST_ASSERT(maybeList);
    LOG(*maybeList);

    // Check that the output has two lines. The output can be quite complicated,
    // and doesn't have a fixed format, so I think it's only practical to
    // assume that the output will have one line per file, and that if the number
    // if files is correct then whether the output is correct is up to the server
    // (assuming we haven't wrongly decoded it).
    TEST_ASSERT(std::count(maybeList->cbegin(), maybeList->cend(), '\n') == 2);
  }
  },

  { "Test list non-empty directory via path",
  [](Client &client, const path &, const path &) {
    assertConnectAndLogin(client);

    const auto maybeList = client.list("files");
    TEST_ASSERT(maybeList);
    LOG(*maybeList);

    TEST_ASSERT(std::count(maybeList->cbegin(), maybeList->cend(), '\n') == 2);
  }
  },

  // Annoyingly, vsftpd seems to send an empty string when a client tries to
  // list a directly that doesn't exist.
  //
  // { "Test list non-existent dir",
  // [](Client &client, const path &, const path &serverTemp) {
  //   constexpr const auto DIR_NAME = "myDirWhichDoesNotExist";
  //   assert(!exists(serverTemp/DIR_NAME));
  //
  //   assertConnectAndLogin(client);
  //
  //   // ~~Should receive a null optional if the dir doesn't exist.~~
  //   LOG(*client.list(std::string("temp/") + DIR_NAME));
  //   TEST_ASSERT(!true);
  // }
  // }

  { "Test append",
  [](Client &client, const path &, const path &serverTemp) {
    const path fileToUpload("scratch/files/bigfile-2049.txt");
    assert(exists(fileToUpload));
    assertConnectAndLogin(client);

    // First upload should create a new file.
    const auto uploadedFile(serverTemp/"newfile.txt");
    TEST_ASSERT(client.appe(fileToUpload, "temp/newfile.txt"));
    TEST_ASSERT(exists(uploadedFile) && file_size(uploadedFile) == 2049);

    // Second upload should append.
    TEST_ASSERT(client.appe(fileToUpload, "temp/newfile.txt"));
    TEST_ASSERT(exists(uploadedFile) && file_size(uploadedFile) == 2 * 2049);
  }
  },

  { "Test rename",
  [](Client &client, const path &, const path &serverTemp) {
    assertConnectAndLogin(client);

    const path fileToRename(serverTemp/"oldfilename.txt");
    // Create the temp file.
    TEST_ASSERT(std::ofstream(fileToRename));

    TEST_ASSERT(client.rename("temp/oldfilename.txt", "temp/newfilename.txt"));

    const path renamedFile(serverTemp/"newfilename.txt");
    TEST_ASSERT(exists(renamedFile));
    TEST_ASSERT(!exists(fileToRename));
  }
  },

  { "Test rename of non-existent file",
  [](Client &client, const path &, const path &serverTemp) {
    assertConnectAndLogin(client);

    const path fileToRename(serverTemp/"myFileWhichDoesNotExist.txt");
    TEST_ASSERT(!exists(fileToRename));

    TEST_ASSERT(!client.rename("temp/myFileWhichDoesNotExist.txt", "temp/myNewName.txt"));
  }
  },

  { "Test rename file to itself",
  [](Client &client, const path &, const path &serverTemp) {
    assertConnectAndLogin(client);

    const path fileToRename(serverTemp/"newfile.txt");
    // Create the file.
    TEST_ASSERT(std::ofstream(fileToRename));

    // Not really something on our end, but interesting to test.
    TEST_ASSERT(client.rename("temp/newfile.txt", "temp/newfile.txt"));
  }
  },

  // There are not as many tests as I'd like for login.
  // It's difficult to test all the cases because they
  // require different server configurations.
  // The tests don't get to specify a server configuration;
  // they all the same one and are unaware of how it is launched.
  // It's probably possible to re-launch the server automatically
  // before each test but I think it's not worth the benefit,
  // for this simple project.

  { "Test username-only login failure",
  [](Client &client, const path &, const path &) {
    TEST_ASSERT(client.connect(HOST));

    // Not a valid username.
    TEST_ASSERT(!client.login("absjdsfs"));
  }
  },

  { "Test username & password login succeed",
  [](Client &client, const path &, const path &) {
    TEST_ASSERT(client.connect(HOST));

    // These are the same credentials used for the rest of the tests.
    TEST_ASSERT(client.login("anonymous", "anonymous"));
  }
  },

  // Looks like vsftpd doesn't support logging in with account info.
  // Probably should have checked that before I implemented it -- I guess
  // it's not used very often...
  // { "Test username, password & account info login",
  // [](Client &client, const path &, const path &) {
  //   TEST_ASSERT(client.connect(HOST));

  //   TEST_ASSERT(client.login("anonymous", "anonymous", "guest"));
  // }
  // }
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


  const std::vector<std::string> testAllowList{

  };


  for (const auto &[name, testFunc] : tests) {
    if (!testAllowList.empty() && std::find(testAllowList.cbegin(), testAllowList.cend(), name) == testAllowList.cend()) {
      // Skip this test because the allow list is populated and this test isn't in it.
      continue;
    }
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
