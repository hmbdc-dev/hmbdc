#
# the Makefile is for reference purpose
#
exes := $(basename $(wildcard example/*.cpp)) $(basename $(wildcard tools/*.cpp)) tools/tips-ping-pong-rt
COMPILE_OPTIONS:=-std=c++1z -fconcepts -Wno-parentheses -Wno-attribute-warning -Wno-vla-cxx-extension -D BOOST_BIND_GLOBAL_PLACEHOLDERS -Wall -Werror -lboost_program_options -pthread -lrt -O3 -ldl -I.
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
	
# add a build flag to generate tools/tips-ping-pong-rt
tools/tips-ping-pong-rt : tools/tips-ping-pong.cpp
	@echo building $@ ... patience ...
	$(CXX)  $< $(COMPILE_OPTIONS) -DPINGPONG_RT -o $@

.PHONY : lic
lic :
	cp hmbdc/Copyright.hpp LICENSE.TXT

.PHONY : docs
docs :
	@(which doxygen 2>&1) > /dev/null && doxygen doxygen-all.cfg || echo "Cannot detect installed doxygen, skip doc generation!"

.PHONY : clean
clean :
	rm -rf $(exes) LICENSE.TXT doc

	
