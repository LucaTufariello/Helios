.PHONY: build run build-shared build-desktop test-shared test-desktop test-raster setup-raster format format-check check

build:
	./native/desktop/build.sh

run: build
	open output/desktop/build/bin/Helios.app

build-shared:
	cmake -S . -B output/shared/build -DCMAKE_BUILD_TYPE=Release
	cmake --build output/shared/build --parallel 2

test-shared: build-shared
	ctest --test-dir output/shared/build --output-on-failure

build-desktop:
	./native/desktop/build.sh

test-desktop: build-desktop
	ctest --test-dir output/desktop/build --output-on-failure

test-raster:
	./native/workers/test.sh

setup-raster:
	./native/workers/setup-raster.sh

format:
	./native/format.sh --write

format-check:
	./native/format.sh --check

check: format-check test-shared test-desktop test-raster
