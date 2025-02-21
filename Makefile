
#CFLAGS += -Wpedantic -pedantic-errors
CFLAGS += -Werror
CFLAGS += -Wall
CFLAGS += -Wextra
CFLAGS += -Wcast-align
CFLAGS += -Wno-cast-qual	# free() should accept const pointers
CFLAGS += -Wfloat-equal
CFLAGS += -Wformat=2
CFLAGS += -Wlogical-op
CFLAGS += -Wmissing-include-dirs
CFLAGS += -Wno-missing-declarations
CFLAGS += -Wpointer-arith
CFLAGS += -Wredundant-decls
CFLAGS += -Wsequence-point
CFLAGS += -Wshadow
CFLAGS += -Wswitch
CFLAGS += -Wundef
CFLAGS += -Wunreachable-code
CFLAGS += -Wunused-but-set-parameter
CFLAGS += -Wno-unused-parameter
CFLAGS += -Wno-maybe-uninitialized
CFLAGS += -Wwrite-strings

CPPFLAGS := $(CFLAGS)
CPPFLAGS += -std=c++20

#CFLAGS += -Waggregate-return
CFLAGS += -Wbad-function-cast
CFLAGS += -Wno-declaration-after-statement
CFLAGS += -Wno-missing-prototypes
CFLAGS += -Wno-strict-prototypes
CFLAGS += -Wnested-externs

# For some submissions to compile:
CFLAGS += -Wno-unused-variable
CFLAGS += -Wno-unused-result

BINARIES += mm mm_test

all: $(BINARIES)

OPT = -O0

define do-link-c
	gcc -g $(OPT) $^ -o $@
endef

define do-link-cc
	g++ -g $(OPT) $^ -o $@ -lstdc++
endef

define do-c
	gcc -g $(OPT) -x c $(CFLAGS) $< -c -o $@ -MD -MF $(@:.o=.d)
endef

define do-cc
	g++ -g $(OPT) -x c++ $(CPPFLAGS) $< -c -o $@ -MD -MF $(@:.o=.d)
endef

mm: mm_main.o mm_api.o
	$(call do-link-c)

mm_test: mm_test.o mm_api.o
	$(call do-link-cc)

%.o: %.c Makefile
	$(call do-c)

%.o: %.cc Makefile
	$(call do-cc)

project3.zip: FORCE
	rm -rf $@ project3/ && mkdir project3/
	cp mm_main.c mm_api.h mm_api.c mm_test.cc Makefile project3/
	zip -r $@ project3/
	cd project3 && make && rm -rf project3
	@echo Submission zip is here
	ls -ltrh $@

project3_starter.zip: FORCE
	rm -rf $@ project3/ && mkdir project3/
	cp mm_main.c mm_api.h mm_test.cc Makefile project3/
	cp mm_api_provided.c project3/mm_api.c
	zip -r $@ project3/
	cd project3 && make && rm -rf project3
	ls -ltrh $@

clean:
	rm -f *.o *.d mm_test.out project3.zip project3_starter.zip $(BINARIES)

SUBMISSIONS_DIR=../grading/assignment_4/

mm_test.out: mm_test.littlemem
	./$< > $@

mm_test.out.%: mm_test.%
	./$< > $@

bonus.out: mm_test.out.littlemem mm_test.out.tinymem mm_test.out.bigmem
	cat $^ > $@

grading.txt: grading_extra.txt mm_test.out
	cat $^ > $@

test_submissions: FORCE
	for d in $(SUBMISSIONS_DIR)/*/project3/ ; do \
		echo $$d ; \
		(cd $$d && ln -fs ../../../../project3/Makefile && ln -fs ../../../../project3/mm_test.cc && make grading.txt && grep scored grading.txt) ; \
	done

clean_submissions: FORCE
	make clean
	for d in $(SUBMISSIONS_DIR)/*/project3/ ; do \
		echo $$d ; \
		(cd $$d && make clean) ; \
	done
	
.PHONY: all clean FORCE

-include *.d

