PREFIX ?=
QUEUE ?=

.PHONY: all build test install uninstall migrate clean check

all: build

build:
	./build.sh

test: build
	./test.sh

install: build
	sudo $(if $(QUEUE),QUEUE=$(QUEUE)) ./install.sh

uninstall:
	sudo $(if $(QUEUE),QUEUE=$(QUEUE)) ./uninstall.sh

migrate:
	sudo $(if $(QUEUE),QUEUE=$(QUEUE)) ./migrate.sh

check: test

clean:
	rm -rf out
