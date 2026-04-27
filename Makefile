BUILD_DIR := build
CONFIGURE := cmake -S . -B $(BUILD_DIR)
BUILD := cmake --build $(BUILD_DIR)

.PHONY: all configure run clean fclean re

all: configure
	$(BUILD)

configure:
	$(CONFIGURE)

run: all
	./$(BUILD_DIR)/bin/ft_vox

clean:
	cmake --build $(BUILD_DIR) --target clean

fclean:
	rm -rf $(BUILD_DIR)

re: fclean all
