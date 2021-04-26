#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include "varlist.h"

/* Add a name:value pair to the end of a 'varlist'.
   Cease to function on allocation errors */

void addvar(varlist *vars, char *name, char *value) {
  if ((*vars) != NULL) {
    addvar(&((*vars)->next),name,value);
  } else {
    (*vars)=(struct varnode *)malloc(sizeof(struct varnode));
    if (!(*vars)) {
      /*	  perror("malloc()");*/
      exit(17);
    }
    (*vars)->next=NULL;
    (*vars)->name=(char *)malloc(strlen(name)+1);
    if (!((*vars)->name)) {
      /*	  perror("malloc()");*/
      exit(17);
    }
    strcpy((*vars)->name,name);
    (*vars)->value=(char *)malloc(strlen(value)+1);
    if (!((*vars)->value)) {
      /*	  perror("malloc()");*/
      exit(17);
    }
    strcpy((*vars)->value,value);
  }
}

char *findvar(varlist vars, char *name) {
  varlist tmplist=vars;
  while (tmplist) {
    if (!strcasecmp(tmplist->name,name)) {
      return tmplist->value;
    }
    tmplist=tmplist->next;
  }
  return NULL;
}

/* Return all the memory allocated to a 'varlist' */

void freevarlist(varlist *vars) {
  if ((*vars)==NULL) {
    return;
  }
  freevarlist(&((*vars)->next));
  free((*vars)->name);
  free((*vars)->value);
  free((*vars));
  (*vars)=NULL;
}

/* Add an item to the end of a 'namelist'.
   Cease to function on allocation errors */

void addname(namelist *names, char *name) {
  if ((*names) != NULL) {
    addname(&((*names)->next),name);
  } else {
    (*names)=(struct namenode *)malloc(sizeof(struct namenode));
    if (!(*names)) {
      /*	  perror("malloc()");*/
      exit(17);
    }
    (*names)->next=NULL;
    (*names)->name=(char *)malloc(strlen(name)+1);
    if (!((*names)->name)) {
      /*	  perror("malloc()");*/
      exit(17);
    }
    strcpy((*names)->name,name);
  }
}

/*
  Find an item in 'namelist'. Return 0 if not found, otherwise something
  else.
*/

int findname(namelist names, char *name) {
  namelist tmplist=names;
  while (tmplist && (strcasecmp(tmplist->name, name))) {
    tmplist=tmplist->next;
  }
  return (tmplist!=NULL);
}

/* Return all the memory allocated to a 'namelist' */

void freenamelist(namelist *names) {
  if ((*names)==NULL) {
    return;
  }
  freenamelist(&((*names)->next));
  free((*names)->name);
  free((*names));
  (*names)=NULL;
}

/* Remove line breaks at end of string */

int choppable(char c) {
  return (isspace(c));
}

void chop(char *string) {
  if (string) {
    while (strlen(string) && choppable(string[strlen(string)-1])) {
      string[strlen(string)-1]='\0';
    }
  }
}

/* Remove all blanks at the beginning and end of a string */

void cleanupstring(char *string) {
  int i=strlen(string)-1;
  while (i>0 && isspace(string[i])) {
    i--;
  }
  string[i+1]='\0';
  i=0;
  while (isspace(string[i])) {
    i++;
  }
  memmove(string,&(string[i]),strlen(&(string[i]))+1);
}

/* Split a string into substrings, returning the number of substrings found. */

int splitstring(char *string, char splitter, namelist *substrings) {
  char *p1, *p2;  /* Pointers for walking the string */
  char *tmpbuf;
  int found=0;
  if (!(tmpbuf=(char*)malloc(strlen(string)+1))) {
    return 0;
  }
  strcpy(tmpbuf,string); /* Preserve the original string */
  p1=tmpbuf;
  while (*(p1)) {
    if ((p2=strchr(p1,splitter))!=NULL) {
      *(p2++)='\0';
      cleanupstring(p1);  /* cleanupstring() is supposed to be able to handle this */
      addname(substrings,p1);
      found++;
      p1=p2;
    } else {
      if (strlen(p1)>0 || found==0) {
	cleanupstring(p1);  /* cleanupstring() is supposed to be able to handle this */
	addname(substrings, p1);
	p1+=strlen(p1);
	found++;
      }
    }
  }
  free(tmpbuf);
  return found;
}

