.PHONY: test sdk build build-awdl abi tools

test:
	python3 scripts/test.py

sdk:
	python3 scripts/bootstrap_sdk.py

build:
	python3 scripts/build_sequoia.py --variant wifi

build-awdl:
	python3 scripts/build_sequoia.py --variant awdl

abi:
	python3 scripts/build_sequoia.py --native-abi-test --out build/native-abi

tools:
	python3 scripts/build_awdlctl.py
