all:
	mkdir -p build/
	cd build/ && \
		cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release ../ && \
		make

debug:
	mkdir -p build/
	cd build/ && \
		cmake -G "UnixMakefiles" -DCMAKE_BUILD_TYPE=Debug ../ && \
		make

install:
	cd build/ && \
		make install
		ldconfig

clean:
	rm -rf build/
