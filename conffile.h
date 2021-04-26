#ifndef CONFFILE_H
#define CONFFILE_H

#include "varlist.h"

/*********************************
 *
 *
 * Functions for iterating over entries of a specific type
 *
 *
 *********************************/


/*
  Open the file named by 'conffilename' (in) or rewind it if open.

  Returns 0 for failure and !=0 for success
*/
int conf_init(char *conffilename);

/*
  Rewind the current file.
*/
void conf_rewind();

/*
  Get the next entry of type 'type' (in) and return the name in
  'label' (in/out) of max size 'labelsize' (in). Return all attributes
  in 'vars' (in/out).

  Returns 0 for failure or EOF and !=0 for success
*/
int conf_next(char *type, char *label, int labelsize, varlist *vars);

/*
  Close the configuration file and do any additional cleaning up.
*/
int conf_cleanup();

/**********************************/

/**********************************
 *
 *
 * Functions for searching for or changind configuration data
 *
 *
 **********************************/
 
/*
  Find an entry of type 'type' (in) and with then name 'label' (in) in then
  configuration file named by 'conffilename' (in) and return its attributes in
  'vars' (in/out).

  Returns 0 for failure and !=0 for success
*/
int conf_find(char *conffilename, char *type, char *label, varlist *vars);

/*
  Find all entries of type 'type' (in) in the file named by 'conffilename' (in)
  which has 'value' (in) as the value of its attribute 'varname' (in).
  Returns the list of entries in 'names' (in/out).

  Returns 0 for failure and !=0 for success
*/
int conf_matchlist(char *conffilename, char *type, char *varname,
		   char *value, namelist *names);
int conf_getvar(char *conffilename, char *type, char *label,
		char *varname, char *varvalue, int maxlen);
int conf_set(char *conffilename, char *type, char *label,
	     char *name, char *value);


#endif
