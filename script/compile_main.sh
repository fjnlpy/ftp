g++ -std=c++17 -pthread -I/usr/local/boost_1_77_0 -I./include src/ftp/Client.cpp src/main.cpp -o build/out.a && echo "Running..." && ./build/out.a
