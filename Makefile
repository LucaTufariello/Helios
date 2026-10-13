.PHONY: build run build-shared build-desktop test test-shared test-desktop test-raster setup-raster format format-check check

build:
	./native/macos/build-app.sh

run: build
	open output/native/GodEye.app

build-shared:
	cmake -S . -B output/shared/build -DCMAKE_BUILD_TYPE=Release
	cmake --build output/shared/build --parallel 2

test-shared: build-shared
	ctest --test-dir output/shared/build --output-on-failure

build-desktop:
	./native/desktop/build.sh

test-desktop: build-desktop
	ctest --test-dir output/desktop/build --output-on-failure

test:
	./native/macos/test.sh

test-raster:
	./native/macos/test-sar.sh

setup-raster:
	./native/macos/setup-sar.sh

format:
	./native/macos/format.sh --write

format-check:
	./native/macos/format.sh --check

check: format-check test-shared test test-raster
