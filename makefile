TOOLCHAIN_PREFIX=


CXXFLAGS=-std=c++17
CFLAGS= 
LDFLAGS=-static
LDLIBS= 

CC = $(TOOLCHAIN_PREFIX)gcc
CXX = $(TOOLCHAIN_PREFIX)g++
PYTHON = python3

default: filebrowser

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -o $@ -c $<

%.o: %.c
	$(CC) $(CFLAGS) -o $@ -c $<

filebrowser: web.o base64.o
	$(CXX) $(LDFLAGS)  web.o base64.o $(LDLIBS) -o filebrowser

html:
	$(PYTHON) convert_html.py
	rm *.o filebrowser
	
clean:
	rm *.o filebrowser
