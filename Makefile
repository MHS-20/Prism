BUILD_DIR ?= build

.PHONY: build clean rebuild run

build:
	@cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Release
	@cmake --build $(BUILD_DIR)

clean:
	@rm -rf $(BUILD_DIR)

rebuild: clean build

run: build
	@$(BUILD_DIR)/prism
