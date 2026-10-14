MINEMU ?= minemu

.PHONY: all prepare-hello prepare-devices prepare-ioctl-checks clean

all: prepare-hello prepare-devices prepare-ioctl-checks

prepare-hello:
	$(MAKE) -C fixtures/hello MINEMU="$(MINEMU)" all

prepare-devices:
	$(MAKE) -C fixtures/devices MINEMU="$(MINEMU)" all

prepare-ioctl-checks:
	$(MAKE) -C fixtures/ioctl-checks MINEMU="$(MINEMU)" all

prepare-%:
	@:

clean:
	$(MAKE) -C fixtures/hello clean
	$(MAKE) -C fixtures/devices clean
	$(MAKE) -C fixtures/ioctl-checks clean
