CC = g++
CFLAGS = -std=c++11 -L/usr/local/lib -lboost_serialization
CFLAGS_SO = -I`python -c 'from distutils.sysconfig import *; print get_python_inc()'` -DPIC -bundle -fPIC -lboost_python -framework Python -std=c++11 -L/usr/local/lib -lboost_serialization -O2

python_vpylm:
	$(CC) python_vpylm.cpp -o vpylm.so $(CFLAGS_SO)

python_hpylm:
	$(CC) python_hpylm.cpp -o hpylm.so $(CFLAGS_SO)

hpylm:
	$(CC) hpylm.cpp $(CFLAGS) -O2

hpylm_debug:
	$(CC) hpylm.cpp $(CFLAGS) -g

vpylm:
	$(CC) vpylm.cpp $(CFLAGS) -O2

vpylm_debug:
	$(CC) vpylm.cpp $(CFLAGS) -g

clean:
	rm -rf vpylm.so hpylm.so a.out

.PHONY: vpylm