CFLAGS = -std=c++11 -w -g

p2pim : p2pim.cpp p2pim.h
	$(CXX) $(CFLAGS) $^ -o $@
clean :
	rm -rf p2pim
