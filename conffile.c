/*
 * This is ancient code. I've just added brackets
 * and updated indentation, and not tried to check
 *  whether anything can be improved.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <ctype.h>
#include <unistd.h>
#include <ctype.h>

#include "conffile.h"

#define CONFFILE_BUFSIZE 1024

#define LINE_EOF -1
#define LINE_OK 0
#define LINE_SKIP 1
#define LINE_EMPTY 2

#define min(a,b) (((a)<(b))?(a):(b))

/***************************************

  The (global) configuration file handle and
  functions for reading from it

****************************************/

static FILE *conffile=NULL;

/* Rewind the configuration file to the beginning. */

void conf_rewind() {
  if (conffile)
    fseek(conffile,0,SEEK_SET);
}

/* Open the configuration file. Return 0 on failure. If the file
   is already open, just rewind it to the beginning. */

int conf_init(char *conffilename) {
  if (!conffile) {
    conffile=fopen(conffilename,"r");
  } else {
    conf_rewind();
  }
  if (conffile) {
    return 1;
  } else {
    perror(conffilename);
    return 0;
  }
}

int getnextline(char *buffer, int buffersize, FILE *file, char *orgbuffer, int orgbuffersize) {
  char *p;
  if (!file) {
    return LINE_EOF;
  }
  if (!fgets(buffer, buffersize, file)) {
    return LINE_EOF;
  }
  if (orgbuffer) {
    if (orgbuffersize < strlen(buffer)+1) {
      return LINE_SKIP;
    } else {
	strcpy(orgbuffer, buffer);
    }
  }
  /* Remove whitespace from beginning and end */
  cleanupstring(buffer);

  /* If the string is empty now BEFORE removing comments,
     we really consider it empty */
  if (!strlen(buffer)) {
    return LINE_EMPTY;
  }

  /* Now, remove comments */
  if ((p=strchr(buffer,'#')) != NULL) {
    *p='\0';
  }

  /* If the string is empty now, we just skip it */
  if (!strlen(buffer)) {
    return LINE_SKIP;
  }

  /* Otherwise, return it as is. */
  return LINE_OK;
}

int splitheader(char *buffer, char *type, int typelen, char *name, int namelen) {
  char *p1=buffer;
  int len;
  
  /* Search for the first whitespace */
  while ((*p1) && !isspace(*p1)) {
    p1++;
  }

  /* Not found - syntax error */
  if (!(*p1)) {
    return 0;
  }

  /* Save the type - the first word */
  len=min((p1-buffer), typelen);
  memcpy(type,buffer,len);
  type[len]='\0';

  /* Save the name - the second word */
  strncpy(name,p1,namelen);
  name[namelen-1]='\0';

  /* Remove whitespace */
  cleanupstring(type);
  cleanupstring(name);
  return 1;
}

int splitparmline(char *buffer, char *name, int namelen, char *value, int vallen) {
  char *p1=buffer;
  int len;
  
  /* No equals sign - syntax error */
  if (!(p1=strchr(buffer,'='))) {
    return 0;
  }

  /* Save the attribute name */
  len=min((p1-buffer),namelen);
  memcpy(name,buffer,len);
  name[len]='\0';
  
  /* Walk past the equals sign */
  p1++;
  
  /* And save the value */
  strncpy(value,p1,vallen);
  value[vallen-1]='\0';
  
  /* Remove whitespace */
  cleanupstring(name);
  cleanupstring(value);
  return 1;
}

/* Find the next configuration file entry of type <type> and fill in
   'label' (to a maximum of 'labelsize' chars) and the variable list 'vars'
   Return 1 if found something or 0 if nothing found (end of file) */

int conf_next(char *type, char *label, int labelsize, varlist *vars) {
  int rval;
  int inheader=1, found=0;
  static char inbuf[CONFFILE_BUFSIZE];
  static char thislabel[CONFFILE_BUFSIZE], thistype[CONFFILE_BUFSIZE];
  static char thisvarname[CONFFILE_BUFSIZE], thisvalue[CONFFILE_BUFSIZE];
  
  if (!conffile) {
    return 0;
  }

  while ((rval=getnextline(inbuf, CONFFILE_BUFSIZE, conffile, NULL, 0)) != LINE_EOF) {
#ifdef DEBUG
    fprintf(stderr,"Line type %d: \"%s\"\n",rval,inbuf);
    fflush(stderr);
#endif
    switch (rval) {
    case LINE_SKIP:
      break;
    case LINE_EMPTY:
      inheader=1;
      /* If we found an entry, return */
      if (found) {
	strncpy(label, thislabel, labelsize);
	label[labelsize-1]='\0';
	return 1;
      }
      break;
    default:
      if (inheader) {
	if (splitheader(inbuf, thistype, sizeof(thistype),
			thislabel, sizeof(thislabel))) {
#ifdef DEBUG
	  fprintf(stderr,"Header: type=\"%s\", name=\"%s\"\n",
		  thistype, thislabel);
	  fflush(stderr);
#endif
	  if (!strcasecmp(thistype,type))
	    found=1;
	}

	/* A header is always exactly one line here.. */
	inheader=0;
      } else {
	/* Here we know that we are in the "body" of a stanza */
	if (found) {
	  
	  /* Here we know that this stanza is of the correct type,
	     so we have to extract the variables. Save their names and values in
	     the list provided in the call */
	  if (splitparmline(inbuf,thisvarname, sizeof(thisvarname),
			    thisvalue, sizeof(thisvalue))) {
#ifdef DEBUG
	    fprintf(stderr,"Parm:   name=\"%s\", value=\"%s\"\n",
		    thisvarname, thisvalue);
	    fflush(stderr);
#endif
	    addvar(vars,thisvarname,thisvalue);
	  }
	}
      }
    }
  }
  if (found) {
    strncpy(label,thislabel,labelsize);
    label[labelsize-1]='\0';
    return 1;
  } else {
    return 0;
  }
}

/* Change or set a value */
int conf_set(char *conffilename, char *type, char *label, char *name, char *value) {
  int rval;
  int inheader=1, found=0, done=0;
  static char inbuf[CONFFILE_BUFSIZE], inbuf2[CONFFILE_BUFSIZE];
  static char tmpfilename[CONFFILE_BUFSIZE];
  static char thislabel[CONFFILE_BUFSIZE], thistype[CONFFILE_BUFSIZE];
  static char thisvarname[CONFFILE_BUFSIZE], thisvalue[CONFFILE_BUFSIZE];
  FILE *tmpfile;
  
  conf_init(conffilename);
  if (!conffile) {
    fprintf(stderr,"[creating new file]\n");
  }

  /* This method will not work when "/var" is on a different filesystem.
     Tough. */
  sprintf(tmpfilename,"/var/tmp/%08lX-%04X",time(NULL),getpid());
  if (!(tmpfile=fopen(tmpfilename,"w"))) {
    return 0;
  }

  while ((rval=getnextline(inbuf, sizeof(inbuf), conffile, inbuf2, sizeof(inbuf2))) !=
	 LINE_EOF) {
    switch (rval) {
    case LINE_SKIP:
      fwrite(inbuf2,1,strlen(inbuf2),tmpfile);
      break;
    case LINE_EMPTY:
      inheader=1;
      /* If we found an entry, return */
      if (found && !done) {
	fprintf(tmpfile,"\t%s=%s\n", name, value);
	done=1;
	found=0;
      }
      fwrite(inbuf2,1,strlen(inbuf2),tmpfile);
      break;
    default:
      if (inheader) {
	if (splitheader(inbuf, thistype, sizeof(thistype),
			thislabel, sizeof(thislabel))) {
	  if ((!strcasecmp(thistype,type)) && (!strcasecmp(thislabel, label)))
	    found=1;
	}
	
	/* A header is always exactly one line here.. */
	inheader=0;
	fwrite(inbuf2,1,strlen(inbuf2),tmpfile);
      } else {
	/* Here we know that we are in the "body" of a stanza of the correct type,
	   so we have to extract the variables. Save their names and values in
	   the list provided in the call */
	if (found && 
	    !done &&
	    splitparmline(inbuf,thisvarname, sizeof(thisvarname),
			  thisvalue, sizeof(thisvalue)) && 
	    !strcasecmp(thisvarname, name)) {
	  fprintf(tmpfile,"\t%s=%s\n", name, value);
	  done=1;
	  found=0;
	} else {
	  fwrite(inbuf2,1,strlen(inbuf2),tmpfile);
	}
      }
      
    }
  }

  if (!done) {
    if (!found)	{
      if (!inheader) {
	fprintf(tmpfile,"\n");
      }
      fprintf(tmpfile,"%s %s\n",type, label);
    }
    fprintf(tmpfile,"\t%s=%s\n",name,value);
  }
  conf_cleanup();
  fclose(tmpfile);
  sprintf(inbuf,"%s-",conffilename);
  sprintf(inbuf2,"%s--",conffilename);
  rename(inbuf,inbuf2);
  rename(conffilename,inbuf);
  if (!rename(tmpfilename,conffilename)) {
    return 0;
  } else {
    return 1;
  }
}

/* Close the configuration file */

int conf_cleanup() {
  if (conffile) {
    fclose(conffile);
  }
  conffile=NULL;
  return 1;
}

/**************************************

 This helper function finds one entry by name ('label') and
 fills in the 'varlist' with its values

**************************************/

int conf_find(char *conffilename, char *type, char *label, varlist *vars) {
  char thislabel[CONFFILE_BUFSIZE];
  conf_init(conffilename);
  while (conf_next(type, thislabel, sizeof(thislabel), vars)) {
    if (!strcasecmp(label, thislabel)) {
      conf_cleanup();
      return 1;
    }
    freevarlist(vars);
  }
  conf_cleanup();
  return 0;
}

/**************************************

 This function returns a list of the names of all entries matching the 
 [varname,value] pair.

***************************************/

int conf_matchlist(char *conffilename, char *type, char *varname,
		   char *value, namelist *names) {
  varlist myvars=NULL, tmpvar;
  char thislabel[CONFFILE_BUFSIZE];
  int found=0;
  conf_init(conffilename);
  while (conf_next(type, thislabel, sizeof(thislabel), &myvars)) {
    tmpvar=myvars;
    while (tmpvar) {
      if ((!strcasecmp(tmpvar->name, varname) &&
	   (!strcasecmp(tmpvar->value,value)))) {
	addname(names,thislabel);
	found++;
	break;
      }
      tmpvar=tmpvar->next;
    }
    freevarlist(&myvars);
  }
  conf_cleanup();
  return (found);
}

/***********************************************

 This function gets and returns one variable value for one specific
 configuration file entry.

************************************************/

int conf_getvar(char *conffilename, char *type, char *label, char *varname,
		char *varvalue, int maxlen) {
  varlist myvars=NULL, tmpvar;
#ifdef DEBUG
  fprintf(stderr,"Asked for \"%s\" \"%s\" \"%s\" in %s\n",type, label,
	  varname, conffilename);
  fflush(stderr);
#endif
  if (conf_find(conffilename, type, label, &myvars)) {
    tmpvar=myvars;
    while (tmpvar) {
#ifdef DEBUG
      fprintf(stderr,"\tChecking \"%s\"\n", tmpvar->name);
      fflush(stderr);
#endif
      if (!strcasecmp(tmpvar->name, varname)) {
	strncpy(varvalue,tmpvar->value,maxlen);
	varvalue[maxlen-1]='\0';
	freevarlist(&myvars);
	return 1;
      }
      tmpvar=tmpvar->next;
    }
    freevarlist(&myvars);     // Entry found but the value isn't there
    return 0;
  } else {
    return 0;                   // Entry not found
  }
}
