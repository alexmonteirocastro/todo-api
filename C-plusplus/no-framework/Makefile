CXX               ?=     g++
CXXFLAGS          ?=     -std=c++20 -Wall -Wextra
LDLIBS            ?=     -lsqlite3

SRCS = $(wildcard *.cpp)
OBJS = $(SRCS:.cpp=.o)   # transforms the list: main.cpp -> main.o, etc.
HDRS = $(wildcard *.h)   # all .h files in the folder

server: $(OBJS)
	$(CXX) $(CXXFLAGS) -o server $(OBJS) $(LDLIBS)

%.o: %.cpp $(HDRS)
	$(CXX) $(CXXFLAGS) -c $< -o $@

run: server
	./server

clean:
	rm -f server $(OBJS)

reset-db:
	rm -f todos.db

.PHONY: run clean reset-db