NAME        := ft_vox

SRCS_DIR    := ./srcs
OBJS_DIR    := ./objs
VENDOR      := ./vendor

INC         := $(shell find $(SRCS_DIR) -type d | sed 's|^|-I |')
INC         += -I include
INC         += -I $(VENDOR)/glad/include
INC         += -I $(VENDOR)/glm
INC         += -I $(VENDOR)/stb
INC         += -I $(VENDOR)/FastNoiseLite
INC         += -I $(VENDOR)/glfw/include

HEADERS     := $(shell find $(SRCS_DIR) -name "*.hpp")
HEADERS     += $(shell find include -name "*.hpp" 2>/dev/null)
SRCS        := $(shell find $(SRCS_DIR) -name "*.cpp" | sed 's|^|./|')
OBJS        := $(subst $(SRCS_DIR),$(OBJS_DIR),$(SRCS:.cpp=.o))
DEPS        := $(subst $(SRCS_DIR),$(OBJS_DIR),$(SRCS:.cpp=.d))

GLAD_SRC    := $(VENDOR)/glad/src/gl.c
GLAD_OBJ    := $(OBJS_DIR)/glad/gl.o

CXX         := c++
CC          := cc
CXXFLAGS    := -Wall -Wextra -Werror -std=c++17 $(INC) -MMD -MP
CFLAGS_GLAD := $(INC)

UNAME       := $(shell uname)
ifeq ($(UNAME), Darwin)
    LDFLAGS := -L $(VENDOR)/glfw/lib -lglfw3 \
               -framework OpenGL \
               -framework Cocoa \
               -framework IOKit \
               -framework CoreVideo
else
    LDFLAGS := -L $(VENDOR)/glfw/lib -lglfw3 \
               -lGL -ldl -lpthread \
               -lX11 -lXrandr -lXinerama -lXcursor -lXi
endif

ifeq ($(MAKECMDGOALS), debug)
    CXXFLAGS += -DDEBUG -g
endif

ifeq ($(MAKECMDGOALS), address)
    CXXFLAGS    += -g -fsanitize=address
    LDFLAGS     += -fsanitize=address
endif

all         : $(NAME)

$(NAME)     : $(GLAD_OBJ) $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

$(OBJS_DIR)/%.o : $(SRCS_DIR)/%.cpp
	mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -o $@ -c $<

$(GLAD_OBJ) : $(GLAD_SRC)
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS_GLAD) -o $@ -c $<

clean       :
	rm -rf $(OBJS_DIR)

fclean      : clean
	$(RM) $(NAME)

re          : fclean all

debug       : re

address     : re

run         : all
	./$(NAME)

setup       :
	@bash scripts/setup.sh

format      :
	clang-format -i $(SRCS) $(HEADERS)

help        : Makefile
	@echo "Usage: make [target]"
	@echo ""
	@echo "Targets:"
	@echo "  all      - build $(NAME)"
	@echo "  clean    - remove object files"
	@echo "  fclean   - remove object files and $(NAME)"
	@echo "  re       - rebuild from scratch"
	@echo "  debug    - build with DEBUG flag"
	@echo "  address  - build with AddressSanitizer"
	@echo "  run      - build and run $(NAME)"
	@echo "  setup    - download and build dependencies into vendor/"
	@echo "  format   - format source files with clang-format"
	@echo "  help     - show this help"

.PHONY      : all clean fclean re debug address run setup format help

-include $(DEPS)
