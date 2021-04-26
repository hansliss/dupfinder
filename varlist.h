#ifndef VARLIST_H
#define VARLIST_H

/********************************

 A list node for handling variable lists and
 functions to make use of it

********************************/

typedef struct varnode {
  char *name;
  char *value;
  struct varnode *next;
} *varlist;

/*
  Add a attribute/value pair to the list 'vars' (in/out).
  'name' and 'value' are in parameters and their contents
  will be copied.
  */
void addvar(varlist *vars, char *name, char *value);

/*
  Find and return a node in 'vars' (in) with the
  attribute name 'name' (in).
  */
char *findvar(varlist vars, char *name);

/*
  Release all memory allocated for the list 'vars' (in/out) and
  set it to NULL.
  */
void freevarlist(varlist *vars);


/*******************************

 Another list node for handling simple name lists.
 Very useful for splitstring().

 *******************************/

typedef struct namenode {
  char *name;
  struct namenode *next;
} *namelist;

/*
  Add a new name node to 'names' (in/out) with the name 'name' (in).
  'name' will be copied.
  */
void addname(namelist *names, char *name);

/*
  Find and return a node in 'names' (in) with the name 'name' (in)
  and return it, or NULL if nothing found.
  */
int findname(namelist names, char *name);

/*
  Release all memory allocated for the list 'names' (in/out) and
  set it to NULL.
  */
void freenamelist(namelist *names);

/*
  Split the string 'string' (in) into components using the delimiter
  'splitter' (in). Add the components to 'substrings' (in/out), which
  must be initialized prior to calling this function.
  The substrings will be cleaned up with cleanupstring() (see below).
  */
int splitstring(char *string, char splitter, namelist *substrings);

/*
  Remove all blanks at the beginning and end of the string 'string' (in/out).
  */
void cleanupstring(char *string);

/*
  Chop off all whitespace characters (including newline characters etc)
  from the end of the string 'string' (in/out).
  */
void chop(char *string);

#endif
