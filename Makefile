.PHONY: build test vet deb schema-test fcitx5
build:
	mkdir -p dist
	go build -buildvcs=false -trimpath -o dist/sixuan-fcitx5 .
test:
	go test ./...
schema-test:
	bash tests/desktop_schema_test.sh
vet:
	go vet ./...
deb:
	bash packaging/build-deb.sh

FCITX5_CFLAGS := $(shell pkg-config --cflags Fcitx5Core Fcitx5Utils gio-2.0 2>/dev/null)
FCITX5_LIBS := $(shell pkg-config --libs Fcitx5Core Fcitx5Utils gio-2.0 2>/dev/null)

# Standalone Fcitx 5 addon; never links librime (the daemon owns Rime).
fcitx5: dist/libsixuan.so
dist/libsixuan.so: fcitx5/sixuan.cpp | dist
	g++ -std=c++20 -shared -fPIC -O2 $(FCITX5_CFLAGS) $< $(FCITX5_LIBS) -o $@
dist:
	mkdir -p dist
