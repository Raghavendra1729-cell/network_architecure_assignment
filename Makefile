CXX = g++
CXXFLAGS = -std=c++11 -Wall -Wextra -O2 -pthread

SERVER_SRC = src/server.cpp src/http_parser.cpp src/calculator.cpp

all: server test_client

server: $(SERVER_SRC) src/http_parser.h src/calculator.h
	$(CXX) $(CXXFLAGS) -o server $(SERVER_SRC)

test_client: tests/test_client.cpp
	$(CXX) $(CXXFLAGS) -o test_client tests/test_client.cpp

clean:
	rm -f server test_client

.PHONY: all clean
