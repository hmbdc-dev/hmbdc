#
# the Makefile is for reference purpose, if you need netmap functions in the tools remove the HMBDC_NO_NETMAP definition
#
exes := $(basename $(wildcard example/*.cpp)) $(basename $(wildcard tools/*.cpp))
COMPILE_OPTIONS:=-std=c++1z -fconcepts -Wno-parentheses -D HMBDC_NO_NETMAP -D BOOST_BIND_GLOBAL_PLACEHOLDERS -Wall -Werror -lboost_program_options -pthread -lrt -O3 -ldl -I.
ifdef tsan
COMPILE_OPTIONS += -fsanitize=thread
else ifdef asan
COMPILE_OPTIONS += -fsanitize=address -fsanitize=leak
endif
.PHONY : all
all : $(exes) lic docs
% : %.cpp
	@echo building $@ ... patience ...
	$(CXX)  $< $(COMPILE_OPTIONS) -o $@

.PHONY : lic
lic :
	cp hmbdc/Copyright.hpp LICENSE.TXT

.PHONY : docs
docs :
	@(which doxygen 2>&1) > /dev/null && doxygen doxygen-all.cfg || echo "Cannot detect installed doxygen, skip doc generation!"

.PHONY : clean
clean :
	rm -rf $(exes) LICENSE.TXT doc

	
