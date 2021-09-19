.PHONY : all
all : $(basename $(wildcard example/*.cpp)) $(basename $(wildcard tools/*.cpp))
% : %.cpp
	@echo building $@ ... patience ...
	$(CXX)  $< -std=c++1z -fconcepts -Wno-parentheses -D HMBDC_NO_NETMAP -D GTEST_HAS_TR1_TUPLE=0 -D BOOST_BIND_GLOBAL_PLACEHOLDERS -Wall -Werror -lboost_program_options -pthread -lrt -O3 -I. -o $@

	
