CFLAGS = -std=c++11

p2pim : p2pim.cpp
	$(CXX) $(CFLAGS) $^ -o $@
clean :
	rm -rf p2pim