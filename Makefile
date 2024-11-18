NAME = lemipc

#########
RM = rm -rf
CC = cc
CFLAGS = -Werror -Wextra -Wall -g -fsanitize=address
LDFLAGS = -lm
RELEASE_CFLAGS = $(CFLAGS) -DNDEBUG
#########

#########
FILES = main ft_malloc ft_list game 

SRC = $(addsuffix .c, $(FILES))

vpath %.c srcs inc srcs/parse_arg srcs/nmap 
#########

#########
OBJ_DIR = objs
OBJ = $(addprefix $(OBJ_DIR)/, $(SRC:.c=.o))
DEP = $(addsuffix .d, $(basename $(OBJ)))
#########

#########
$(OBJ_DIR)/%.o: %.c
	@mkdir -p $(@D)
	${CC} -MMD $(CFLAGS) -c -Isrcs/nmap -Iinc -Isrcs/parse_arg -Isrcs/nmap $< -o $@

all: .gitignore
	$(MAKE) $(NAME)

$(NAME): $(OBJ) Makefile
	$(CC) $(CFLAGS) $(OBJ) -o $(NAME) $(LDFLAGS)
	@echo "EVERYTHING DONE  "
#	@./.add_path.sh

release: CFLAGS = $(RELEASE_CFLAGS)
release: re
	@echo "RELEASE BUILD DONE  "

clean:
	$(RM) $(OBJ) $(DEP)
	$(RM) -r $(OBJ_DIR)
	@echo "OBJECTS REMOVED   "

.gitignore:
	@if [ ! -f .gitignore ]; then \
		echo ".gitignore not found, creating it..."; \
		echo ".gitignore" >> .gitignore; \
		echo "$(NAME)" >> .gitignore; \
		echo "$(OBJ_DIR)/" >> .gitignore; \
		echo ".gitignore created and updated with entries."; \
	else \
		echo ".gitignore already exists."; \
	fi

MAN_DIR = /usr/local/share/man/man1
MAN_PAGE = lemipc.1

install: $(NAME)
	install -d $(DESTDIR)$(MAN_DIR)
	install -m 644 $(MAN_PAGE) $(DESTDIR)$(MAN_DIR)

uninstall:
	rm -f $(DESTDIR)$(MAN_DIR)/$(MAN_PAGE)

fclean: clean
	$(RM) $(NAME)
	@echo "EVERYTHING REMOVED   "

re:	fclean all

.PHONY: all clean fclean re release .gitignore

-include $(DEP)