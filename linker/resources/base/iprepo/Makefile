# Top-level Makefile that builds 3 subprojects (each has its own Makefile)
SUBDIRS := hbm_bandwidth hls traffic_producer
SUBTARGET ?= all

.PHONY: all clean rebuild $(SUBDIRS) build-% clean-% rebuild-%

all: $(SUBDIRS)

$(SUBDIRS):
	$(MAKE) -C $@ $(SUBTARGET)

clean:
	@for d in $(SUBDIRS); do \
		$(MAKE) -C $$d clean || exit $$?; \
	done

rebuild: clean all

build-%:
	$(MAKE) -C $* $(SUBTARGET)

clean-%:
	$(MAKE) -C $* clean

rebuild-%:
	$(MAKE) -C $* clean
	$(MAKE) -C $* $(SUBTARGET)
