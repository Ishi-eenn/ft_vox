BUILD_DIR := build
CONFIGURE := cmake -S . -B $(BUILD_DIR)
BUILD := cmake --build $(BUILD_DIR)
RUN_ARGS := $(filter-out run, $(MAKECMDGOALS))

.PHONY: all configure run clean fclean re

all: configure
	$(BUILD)

configure:
	$(CONFIGURE)

run: all
	./ft_vox $(RUN_ARGS)

clean:
	if [ -d $(BUILD_DIR) ]; then rm -rf $(BUILD_DIR); fi

fclean: clean
	rm -f ft_vox

re: fclean all

%:
	@:
