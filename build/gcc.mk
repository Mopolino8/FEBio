# Make include file for FEBio on Linux

include $(FEBDIR)build/lnx32.mk

CC = g++

FLG = -O3 -fPIC

INC = -I$(FEBDIR) -I$(FEBDIR)build/include