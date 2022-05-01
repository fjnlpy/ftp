#include <iostream>
#include <vector>
#include <array>
#include <boost/array.hpp>
#include <boost/asio.hpp>

using boost::asio::ip::tcp;

int
main(int argc, char **argv)
{
try {

  constexpr const char *daytimeAddress("129.6.15.28"); 
  boost::asio::io_context ioContext;
  tcp::resolver resolver(ioContext);
  auto endpoints = resolver.resolve(daytimeAddress, "daytime"); // Can throw.

  tcp::socket socket(ioContext);
  boost::asio::connect(socket, endpoints); // Can throw, pass error code to avoid that.

  boost::system::error_code errorCode;
  std::string output;
  while (true) {
    std::array<char, 128> buf;
    size_t n = socket.read_some(boost::asio::buffer(buf), errorCode);

    if (errorCode) {
      break;
    }
    
    output.append(buf.data(), n);
  }

  if (errorCode == boost::asio::error::eof) {
    std::cout << "Transmission completed successfully" << std::endl;
    std::cout << output << std::endl;
  } else {
    std::cout << "Error during transmission; errorCode=" << errorCode << std::endl;
  }
  
  return 0;
} catch (const std::exception &e) { // This is needed because error codes aren't used everywhere.
  std::cout << "Error occurred: " << e.what() << std::endl;
  return 1;
}
}
