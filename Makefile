FILES= code/Makefile \
	code/README.md \
	code/run_psc.sh \
	code/*.h \
	code/*.cpp 

handin.tar: $(FILES)
	COPYFILE_DISABLE=1 tar cvf handin.tar $(FILES)

clean:
	(cd code; make clean)
	(cd examples; make clean)
	rm -f *~ handin.tar
