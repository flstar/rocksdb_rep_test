.PHONY: default clean run

TARGET = Test
SRCS = rocksdb_rep_test.cc
OBJS = $(SRCS:.cc=.o)
DEPS = $(SRCS:.cc=.d)

ROCKSDB_DIR = ../rocksdb
INCS = -I$(ROCKSDB_DIR)/include
LIBS = -L$(ROCKSDB_DIR) -lrocksdb -lpthread -lz -lsnappy -lcrypto

CXX = g++ --std=c++11
CXXFLAGS = -g -MMD -Wall
CXXFLAGS += $(INCS)

default: $(TARGET)

-include $(DEPS)

test: default
	./$(TARGET)

$(TARGET): $(OBJS)
	$(CXX) -o $@ $(OBJS) $(LIBS)

%.o: %.cc
	$(CXX) -o $@ -c $< $(CXXFLAGS)

clean:
	rm -f $(TARGET) $(OBJS)

clean_all: clean
	rm -f $(DEPS)

rebuild: clean default
