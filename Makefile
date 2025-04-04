CC=g++
CFLAGS=-Iinclude -Wall -Wextra -std=c++11
SRC=src/main.cpp src/kernel_interaction.cpp src/ram_usage.cpp src/window.cpp
OBJ=$(SRC:.cpp=.o)
TARGET=kernel_interaction_demo

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) -o $@ $^

%.o: %.cpp
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET)