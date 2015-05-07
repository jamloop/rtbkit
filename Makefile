rtbkit:
	@(cd rtbkit && make nodejs_dependencies && make -kj8 BIN=../bin compile)

rtbkit-tests: rtbkit
	@(cd rtbkit && LD_LIBRARY_PATH=$$LD_LIBRARY_PATH:../bin make BIN=../bin test)

.PHONY: rtbkit
