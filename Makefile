targets	:= p2pim
objs := \
	p2pim.o \
	EncryptionLibrary.o \

CXX := g++
CFLAGS := -std=c++11

ifneq ($(V), 1)
	Q = @
	V = 0
endif

ifeq ($(D), 1)
	CFLAGS += -g
else
	D = 0
endif

all: $(targets)

deps := $(patsubst %.o, %.d, $(objs))
-include $(deps)
DEPFLAGS = -MMD -MF $(@:.o=.d)

$(targets) : $(objs)
	@echo "CXX $@"
	$(Q)$(CXX) $(CFLAGS) -o $@ $^ $(DEPFLAGS)

%.o : %.cpp
	@echo "CXX $@"
	$(Q)$(CXX) $(CFLAGS) -c -o $@ $< $(DEPFLAGS)

clean :
	@echo "CLEAN"
	$(Q)rm -f $(targets) $(objs) $(deps)
