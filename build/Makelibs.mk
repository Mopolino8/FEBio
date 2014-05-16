include ../../make.$(PLAT)

LIBDIR = $(notdir $(CURDIR))

SRC = $(wildcard $(FEBDIR)/$(LIBDIR)/*.cpp)
OBJ = $(patsubst $(FEBDIR)/$(LIBDIR)/%.cpp, %.o, $(SRC))
DEP = $(patsubst $(FEBDIR)/$(LIBDIR)/%.cpp, %.d, $(SRC))

LIB = $(FEBDIR)/lib/lib$(FELIB)_$(PLAT).a

$(LIB): $(OBJ)
	ar -cvr $(LIB) $(OBJ)

%.o: $(FEBDIR)/$(LIBDIR)/%.cpp
	$(CC) -MMD -c $(INC) $(DEF) $(FLG) $<

clean:
	$(RM) *.o *.d $(LIB)

-include $(DEP)
