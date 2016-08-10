all: rtbkit plugins golang

plugins:
	@(cd plugins && cmake -G "Unix Makefiles" && make)

rtbkit:
	@(cd rtbkit && make -kj8 BIN=../bin compile)

rtbkit-tests: rtbkit
	@(cd rtbkit && LD_LIBRARY_PATH=$$LD_LIBRARY_PATH:../bin make BIN=../bin test)

golang:
	@(go get -u all)
	@(cd go/gojl && go build && mv gojl ../../../../bin)
	@(cd go/gobid/cmd/rtbd && go build && mv rtbd ../../../../bin)

.PHONY: rtbkit rtbkit-tests plugins golang
