CFLAGS = -std=c++11 -w -g

p2pim : p2pim.cpp
	$(CXX) $(CFLAGS) $^ p2pim.h -o $@
clean :
	rm -rf p2pim
