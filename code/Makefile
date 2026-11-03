APP_NAME = wireroute

OBJS = wireroute.o validate.o

CXX = mpic++
CXXFLAGS = -Wall -O3 -std=c++20 -m64 -I. -fopenmp -Wno-unknown-pragmas

all: $(APP_NAME)

$(APP_NAME): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $<

clean:
	rm -f *.o $(APP_NAME)
