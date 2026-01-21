CC      ?= gcc
TARGET  := build/dynamic_periodic_task

SRC := \
  src/main.c \
  src/net_core.c \
  src/supervisor.c \
  src/task_routines.c \
  src/task_runtime.c \
  src/utils/event.c \
  src/utils/logger.c

OBJ := $(patsubst %.c,build/%.o,$(SRC))
DEP := $(OBJ:.o=.d)

CPPFLAGS := -Iinclude -Iinclude/utils -D_GNU_SOURCE
CFLAGS   ?= -O2 -std=c11 -Wall -Wextra -pedantic
CFLAGS   += -MMD -MP
LDFLAGS  :=
LDLIBS   := -pthread -lrt -lm

.PHONY: all clean debug

all: $(TARGET)

$(TARGET): $(OBJ)
	@mkdir -p $(@D)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

build/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

debug: CFLAGS := -g -O0 -std=c11 -Wall -Wextra -pedantic -MMD -MP
debug: clean all

clean:
	rm -rf build

-include $(DEP)
