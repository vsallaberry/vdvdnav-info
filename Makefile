# Copyright (c) vincent sallaberry. All rights reserved.
############################################################################################
#
# Makefile
#
############################################################################################

# Binary name and package name
BIN		= dvdnav
# SRCDIR: Folder where sources are. Use Empty rather than '.' if in current directory!
SRCDIR 		=
# DISTDIR: where the dist packages zip/tar.xz are saved
DISTDIR		= ../dist

############################################################################################

CC		= clang
CXX		= $(CC)
CPP		= $(CC)
OBJC		= $(CC)

SED		= sed
GREP		= grep
RM		= rm -f
DATE		= date
TAR		= tar
ZIP		= zip
FIND		= find
PKGCONFIG	= pkg-config

############################################################################################

OPTI		= -O3 -pipe
WARN		= -Wall -Wno-ignored-attributes -Wno-attributes
ARCH		= -march=native
#ARCH		= -arch i386 -arch x86_64
INCS		:= $(shell $(PKGCONFIG) --cflags dvdnav)
LIBS		:= $(shell $(PKGCONFIG) --libs dvdnav)
COMMON_FLAGS	= $(OPTI) $(WARN) $(ARCH) $(INCS) \
		  -DSRC_PATH=\"$(realpath $(*D))\" \
		  -DINCLUDE_SOURCE -DBUILD=$(BUILD) -DVERSION=\"$(VERSION)\"

CFLAGS		= -std=c99 $(COMMON_FLAGS)
CXXFLAGS	= $(COMMON_FLAGS)
OBJCFLAGS	= $(COMMON_FLAGS)
LDFLAGS		= $(ARCH) $(OPTI) $(LIBS)

############################################################################################

rwildcard=$(foreach d,$(wildcard $1*),$(call rwildcard,$d/,$2) $(filter $(subst *,%,$2),$d))

SRC	:= $(call rwildcard,$(SRCDIR), *.c *.cc *.cpp *.m)
INCLUDES:= $(call rwildcard,$(SRCDIR), *.h *.hh *.hpp)

OBJ1	= $(SRC:.c=.o)
OBJ2	= $(OBJ1:.cpp=.o)
OBJ3	= $(OBJ2:.cc=.o)
OBJ	= $(OBJ3:.m=.o)
BUILD 	:= $(shell echo $$((`$(GREP) -m1 -E '^\s*build\s*=' VERSION 2>/dev/null | $(SED) -Ee 's/[^=]*=[ \t]*//'`+1)))
VERSION	:= $(shell ($(GREP) -m1 -E '^\s*version\s*=' VERSION 2>/dev/null || echo "=0.0") | $(SED) -Ee 's/.*=[ \t]*//')
DISTNAME:= $(BIN)_$(VERSION)_$(shell $(DATE) '+%Y-%m-%d_%Hh%M')

############################################################################################

all: $(BIN)

$(BIN): src.inc $(OBJ) Makefile
	$(CC) -o $(BIN) $(LDFLAGS) $(OBJ)
	@$(GREP) -Eq '^\s*build\s*=' VERSION 2>/dev/null \
		&& $(SED) -Ee 's/^[ \t]*build[ \t]*=.*/build = $(BUILD)/' -i '' VERSION \
		|| echo "build = $(BUILD)" >> VERSION

src.inc: $(SRC) $(INCLUDES) Makefile
	@$(SED) -Ee 's/\\/\\\\/g' -e 's/(["])/\\\1/g' \
		-e 's/^(.*)$$/"\1\\n"/' $(SRC) $(INCLUDES) Makefile > src.inc
	@echo "$@ generated."

clean:
	$(RM) $(call rwildcard,$(SRCDIR), *.o *~ *.swp \#*) src.inc

distclean: clean
	$(RM) $(BIN)

dist:
	@mkdir -p "$(DISTDIR)/$(DISTNAME)"
	@cp -Rf . "$(DISTDIR)/$(DISTNAME)"
	@$(RM) -R `$(FIND) "$(DISTDIR)/$(DISTNAME)" -type d -and \( -name '.git' -or 'CVS' -or -name '.hg' -or -name '.svn' \) 2> /dev/null`
	@echo "building dist..."
	@cd "$(DISTDIR)/$(DISTNAME)" && $(MAKE) clean && $(MAKE) && $(MAKE) distclean
	@cd "$(DISTDIR)" && $(TAR) cJf "$(DISTNAME).tar.xz" "$(DISTNAME)"
	@cd "$(DISTDIR)" && $(ZIP) -q -r "$(DISTNAME).zip" "$(DISTNAME)"
	@$(RM) -R "$(DISTDIR)/$(DISTNAME)"
	@echo "archives created: $(DISTDIR)/$(DISTNAME).{zip,tar.xz}"

##########################################################################################

%.o: %.m $(INCLUDES) Makefile
	$(OBJC) -c $< $(OBJCFLAGS) $(CPPFLAGS) -o $@

%.o: %.c $(INCLUDES) Makefile
	$(CC) -c $< $(CFLAGS) $(CPPFLAGS) -o $@

%.o: %.cpp $(INCLUDES) Makefile
	$(CXX) -c $< $(CXXFLAGS) $(CPPFLAGS) -o $@

%.o: %.cc $(INCLUDES) Makefile
	$(CXX) -c $< $(CXXFLAGS) $(CPPFLAGS) -o $@

##########################################################################################

