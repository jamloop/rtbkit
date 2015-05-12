all: rtbkit plugins

plugins:
	@(cd plugins && cmake -G "Unix Makefiles" && make)

rtbkit:
	@(cd rtbkit && make -kj8 BIN=../bin compile)

rtbkit-tests: rtbkit
	@(cd rtbkit && LD_LIBRARY_PATH=$$LD_LIBRARY_PATH:../bin make BIN=../bin test)

.PHONY: rtbkit rtbkit-tests plugins
