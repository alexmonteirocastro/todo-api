CXX               ?=     g++
CXXFLAGS          ?=     -std=c++20 -Wall -Wextra

SRCS = $(wildcard *.cpp)
OBJS = $(SRCS:.cpp=.o)   # transforms the list: main.cpp -> main.o, etc.
HDRS = $(wildcard *.h)   # all .h files in the folder

server: $(OBJS)
	$(CXX) $(CXXFLAGS) -o server $(OBJS)

%.o: %.cpp $(HDRS)
	$(CXX) $(CXXFLAGS) -c $< -o $@

run: server
	./server

clean:
	rm -f server $(OBJS)

.PHONY: run clean