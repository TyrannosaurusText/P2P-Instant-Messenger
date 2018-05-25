CFLAGS = -std=c++11 -g

p2pim : p2pim.o EncryptionLibrary.o
	$(CXX) $(CFLAGS) $^ -o $@

p2pim.o : p2pim.cpp p2pim.h EncryptionLibrary.h
	$(CXX) $(CFLAGS) $^ -c

EncryptionLibrary.o : EncryptionLibrary.cpp EncryptionLibrary.h
	$(CXX) $(CFLAGS) $^ -c

clean :
	rm -rf p2pim p2pim.o EncryptionLibrary.o
