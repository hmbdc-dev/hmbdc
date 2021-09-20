#
# the Makefile is for reference purpose, if you need netmap functions in the tools remove the HMBDC_NO_NETMAP definition
#
exes := $(basename $(wildcard example/*.cpp)) $(basename $(wildcard tools/*.cpp))
.PHONY : all
all : $(exes)
% : %.cpp
	@echo building $@ ... patience ...
	$(CXX)  $< -std=c++1z -fconcepts -Wno-parentheses -D HMBDC_NO_NETMAP -D BOOST_BIND_GLOBAL_PLACEHOLDERS -Wall -Werror -lboost_program_options -pthread -lrt -O3 -I. -o $@

.PHONY : clean
clean :
	rm $(exes)

	
