# SPDX-License-Identifier: GPL-3.0-or-later
VERSION := $(shell cat VERSION)
DIST_NAME := brother-dcp-l2520d-$(VERSION)

QUEUE ?=

.PHONY: all build test install uninstall migrate dist clean check

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

# Source tarball for GPL compliance: contains everything needed to rebuild.
dist:
	rm -rf "$(DIST_NAME)" "$(DIST_NAME).tar.gz"
	git archive --format=tar --prefix="$(DIST_NAME)/" HEAD | gzip > "$(DIST_NAME).tar.gz"
	@echo "built $(DIST_NAME).tar.gz"
	@shasum -a 256 "$(DIST_NAME).tar.gz"

check: test

clean:
	rm -rf out "$(DIST_NAME)" "$(DIST_NAME).tar.gz"
