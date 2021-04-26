#include <stdio.h>
#include <dirent.h>
#include <openssl/md5.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <mysql/mysql.h>
#include <unistd.h>

#include "config.h"
#include "conffile.h"

int verbose=0;

void usage(char *progname) {
  fprintf(stderr, "Usage: %s [-v ...] -c <conffile> -i <instance> <root> [<root> ...]\n", progname);
}

/*
 * Collecting the SQL queries up here will remove them from
 * their context, which is annoying. However, it's also nice
 * to keep them all collected in one place, so here they'll
 * stay.
 */
#define EMPTYDIR "DELETE FROM directory"
#define EMPTYFILE "DELETE FROM file"
#define CREATEDIR "INSERT INTO directory (path, parentid) values (?, ?)"
#define UPDATEDIR "UPDATE directory SET numdups=?,numfiles=?,totsize=? WHERE id=?"
#define CREATEFILE "INSERT INTO file (name, parentid, size, hash) VALUES (?, ?, ?, ?)"
#define COUNTDUPS "UPDATE file SET copies=(SELECT lvl2.c FROM (SELECT hash,COUNT(hash) AS c FROM file GROUP BY hash) AS lvl2 WHERE lvl2.hash=file.hash)"
#define SUMSTEP1 "UPDATE directory SET numdups=(SELECT COUNT(id) FROM file WHERE parentid=directory.id AND copies>1)"
#define READDIRS "SELECT id, numdups from directory"
#define READDIRSALL "SELECT id, parentid, numfiles, totsize, numdups from directory order by path"
#define RECOUNTDIR "UPDATE directory SET numdups=(SELECT COUNT(id) FROM file WHERE parentid=directory.id AND copies>1),numfiles=(SELECT COUNT(id) FROM file WHERE parentid=directory.id),totsize=(SELECT SUM(size) FROM file WHERE parentid=directory.id)"

/*
 * We'll build a tree representing the directory structure,
 * and use it to do depth-first summing. However, we'll also
 * try to keep the database in sync when appropriate.
 * We don't actually keep track of the name or even the inode#
 * here, just the database id.
 */
typedef struct dirinfo {
  long id;
  unsigned long numfiles;
  uint64_t totsize;
  unsigned long numdups;
  // The structure needs to be able to handle adding additional children as we go along,
  // So this array of pointers will grow. It is NULL-terminated.
  int nchildpointers;
  struct dirinfo **children;
} *dirtree;

dirtree newDirinfo() {
  dirtree t = (dirtree)malloc(sizeof(struct dirinfo));
  t->id=0;
  t->numfiles=0;
  t->totsize=0;
  t->numdups=0;
  t->nchildpointers=10;
  t->children=(dirtree*)malloc(sizeof(struct dirinfo*) * t->nchildpointers);
  memset(t->children, 0, sizeof(struct dirinfo*) * t->nchildpointers);
  return t;
}

void addChild(dirtree t, dirtree child) {
  int i;
  for (i=0; t->children[i]; i++); // Just find the next free pointer
  if (i >= t->nchildpointers-1) {
    int new = t->nchildpointers * 0.5;
    t->children=(dirtree*)realloc(t->children, sizeof(struct dirinfo*) * (t->nchildpointers + new));
    memset(&(t->children[t->nchildpointers]), 0, sizeof(struct dirinfo*) * new);
    t->nchildpointers += new;
  }
  t->children[i] = child;
}

/*
 * Whenever there are linked structures, there are recursive free()
 * functions.
 */
void freetree(dirtree *t) {
  int i;
  for (i=0; (*t)->children[i]; i++) {
    freetree(&((*t)->children[i]));
    (*t)->children[i] = NULL;
  }
}

/*
 * Open a file and calculate the md5 hash of its contents.
 * The buffer size 65536 bytes was inspired by someone
 * saying they had determined this to be the best general
 * buffer size for this.
 */
char *md5sum(char *filepath) {
  static unsigned char c[MD5_DIGEST_LENGTH];
  int i;
  FILE *infile = fopen (filepath, "rb");
  MD5_CTX mdContext;
  int bytes;
  unsigned char buf[65536];
  static char hex[2 * MD5_DIGEST_LENGTH];

  if (!infile) {
    perror(filepath);
    return 0;
  }

  MD5_Init (&mdContext);
  while ((bytes = fread (buf, 1, sizeof(buf), infile)) != 0) {
    MD5_Update (&mdContext, buf, bytes);
  }
  MD5_Final (c, &mdContext);
  fclose (infile);
  for(i = 0; i < MD5_DIGEST_LENGTH; i++) {
    sprintf(&(hex[i * 2]), "%02x", c[i]);
  }
  return hex;
}

/*
 * Insert a new directory record in the database.
 * Only the name a parent id are set here, and the
 * new id is returned.
 */
long createDirRecord(MYSQL *db, char *name, long parentid) {
  MYSQL_STMT *stmt=NULL;
  MYSQL_BIND param[2];
  if ((stmt = mysql_stmt_init(db)) == NULL) {
    fprintf(stderr, "mysql_stmt_init(): %s", mysql_error(db));
    exit(-1);
  }

  if (mysql_stmt_prepare(stmt, CREATEDIR, strlen(CREATEDIR)) != 0) {
    fprintf(stderr, "mysql_stmt_prepare(): %s", mysql_error(db));
    exit(-2);
  }

  memset ((void *) param, 0, sizeof (param));
  param[0].buffer_type = MYSQL_TYPE_VARCHAR;
  param[0].buffer = (void *)name;
  param[0].buffer_length = strlen(name);
  param[0].is_null = 0;
  
  param[1].buffer_type = MYSQL_TYPE_LONG;
  param[1].buffer = (void *)&parentid;
  param[1].is_unsigned = 0;
  param[1].is_null = 0;

  if (mysql_stmt_bind_param(stmt, param) != 0) {
   fprintf(stderr, "mysql_bind_param(): %s", mysql_error(db));
   exit(-3);;
  }

  if (mysql_stmt_execute(stmt) != 0) {
   fprintf(stderr, "mysql_execute(): %s", mysql_error(db));
   exit(-4);
  }

  long newid = mysql_insert_id(db);
  mysql_stmt_close(stmt);

  if (verbose >= 2) {
    fprintf(stderr, "Dir: \"%s\", parent id %ld\n", name, parentid);
  }
  return newid;
}

/*
 * Update an existing directory record with the numerical values
 * collected so far.
 */
void updateDirRecord(MYSQL *db, dirtree t) {
  MYSQL_STMT *stmt=NULL;
  MYSQL_BIND param[4];
  if ((stmt = mysql_stmt_init(db)) == NULL) {
    fprintf(stderr, "mysql_stmt_init(): %s", mysql_error(db));
    exit(-1);
  }

  if (mysql_stmt_prepare(stmt, UPDATEDIR, strlen(UPDATEDIR)) != 0) {
    fprintf(stderr, "mysql_stmt_prepare(): %s", mysql_error(db));
    exit(-2);
  }

  memset ((void *) param, 0, sizeof (param));
  param[0].buffer_type = MYSQL_TYPE_LONG;
  param[0].buffer = (void *)&(t->numdups);
  param[0].is_unsigned = 1;
  param[0].is_null = 0;
  
  param[1].buffer_type = MYSQL_TYPE_LONG;
  param[1].buffer = (void *)&(t->numfiles);
  param[1].is_unsigned = 1;
  param[1].is_null = 0;

  param[2].buffer_type = MYSQL_TYPE_LONGLONG;
  param[2].buffer = (void *)&(t->totsize);
  param[2].is_unsigned = 1;
  param[2].is_null = 0;

  param[3].buffer_type = MYSQL_TYPE_LONG;
  param[3].buffer = (void *)&(t->id);
  param[3].is_unsigned = 0;
  param[3].is_null = 0;

  if (mysql_stmt_bind_param(stmt, param) != 0) {
   fprintf(stderr, "mysql_bind_param(): %s", mysql_error(db));
   exit(-3);
  }

  if (mysql_stmt_execute(stmt) != 0) {
   fprintf(stderr, "mysql_execute(): %s", mysql_error(db));
   exit(-4);
  }

  mysql_stmt_close(stmt);
}

/*
 * Create a file record, with name, parent, size and hash value.
 * We don't keep track of this info in memory, only in the database.
 */
void createFileRecord(MYSQL *db, char *name, long parentid, char *hash, uint64_t size) {
  MYSQL_STMT *stmt=NULL;
  MYSQL_BIND param[4];
  if ((stmt = mysql_stmt_init(db)) == NULL) {
    fprintf(stderr, "mysql_stmt_init(): %s", mysql_error(db));
    exit(-1);
  }

  if (mysql_stmt_prepare(stmt, CREATEFILE, strlen(CREATEFILE)) != 0) {
    fprintf(stderr, "mysql_stmt_prepare(): %s", mysql_error(db));
    exit(-2);
  }

  memset ((void *) param, 0, sizeof (param));
  param[0].buffer_type = MYSQL_TYPE_VARCHAR;
  param[0].buffer = (void *)name;
  param[0].buffer_length = strlen(name);
  param[0].is_null = 0;
  
  param[1].buffer_type = MYSQL_TYPE_LONG;
  param[1].buffer = (void *)&parentid;
  param[1].is_unsigned = 0;
  param[1].is_null = 0;

  param[2].buffer_type = MYSQL_TYPE_LONGLONG;
  param[2].buffer = (void *)&size;
  param[2].is_unsigned = 1;
  param[2].is_null = 0;

  param[3].buffer_type = MYSQL_TYPE_VARCHAR;
  param[3].buffer = (void *)hash;
  param[3].buffer_length = 2 * MD5_DIGEST_LENGTH;
  param[3].is_null = 0;

  if (mysql_stmt_bind_param(stmt, param) != 0) {
   fprintf(stderr, "mysql_bind_param(): %s", mysql_error(db));
   exit(-3);
  }

  if (mysql_stmt_execute(stmt) != 0) {
   fprintf(stderr, "mysql_execute(): %s", mysql_error(db));
   exit(-4);
  }

  mysql_stmt_close(stmt);

  if (verbose >= 2) {
    fprintf(stderr, "File %s: parent id %ld, %ld bytes, hash: %s\n", name, parentid, size, hash);
  }
}

/*
 * Traverse the directory structure, building the directory tree both in
 * memory and in the database, as we go along.
 */
void calcdir(MYSQL *db, dirtree *t, char *dirpath, long parentid) {
  struct stat fs;
  struct dirent *de;
  char namebuf[131072];
  (*t) = newDirinfo();
  (*t)->id = createDirRecord(db, dirpath, parentid);
  DIR *d = opendir(dirpath);
  if (!d) {
    perror(dirpath);
    exit(-10);
  }
  errno=0;
  // Loop over the directory, ignoring ".", ".." and anything
  // that's not a subdirectory or file
  while ((de = readdir(d))) {
    if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..") || (de->d_type != DT_DIR && de->d_type != DT_REG)) {
      continue;
    }
    // Create a string with the full path to this item
    sprintf(namebuf, "%s/%s", dirpath, de->d_name);
    switch (de->d_type) {
    case DT_DIR: ;
      // For a subdirectory, we just call this function recursively
      // and handle the subtree before moving on.
      // The result is total number of files and total size - there
      // is no way to find duplicates at this stage.
      dirtree child = NULL;
      calcdir(db, &child, namebuf, (*t)->id);
      (*t)->numfiles += child->numfiles;
      (*t)->totsize += child->totsize;
      addChild(*t, child);
      break;
    case DT_REG:
      // Count the file in the parent dir struct, get the size,
      // and calculate the hash value. This info is then saved
      // to the database.
      if (stat(namebuf, &fs)) {
	perror(namebuf);
	exit(-12);
      }
      (*t)->numfiles++;
      (*t)->totsize += fs.st_size;
      char *hash = md5sum(namebuf);
      createFileRecord(db, de->d_name, (*t)->id, hash, fs.st_size);
      break;
    default:
      break;
    }
  }
  if (errno != 0) {
    perror("readdir()");
    exit(-11);
  }
  // Now update the database for this directory, and finish
  updateDirRecord(db, (*t));
  closedir(d);
}

/*
 * Find the correct directory by id and set the
 * numdups value in it.
 */
int setnumdups1(dirtree t, long id, long numdups) {
  int i, r=0;
  if (t->id == id) {
    if (verbose >= 2) {
      fprintf(stderr, "setnumdups1(%ld, %ld)\n", id, numdups);
    }
    t->numdups = numdups;
    return 1;
  } else {
    for (i=0; t->children[i] && !(r=setnumdups1(t->children[i], id, numdups)); i++);
    return r;
  }
}

/*
 * Sum up the number of duplicates in the directory tree.
 * Note that this requires that we have first counted the
 * duplicate files in each directory first, which is done
 * by a query.
 */
unsigned long sumnumdups(MYSQL *db, dirtree t) {
  int i;
  for (i=0; t->children[i]; i++) {
    t->numdups += sumnumdups(db, t->children[i]);
  }
  updateDirRecord(db, t);
  if (verbose >= 2) {
    fprintf(stderr, "sumnumdups(%ld): %ld\n", t->id, t->numdups);
  }
  return t->numdups;
}

/*
 * Add up the number of files in each subtree
 */
unsigned long sumnumfiles(dirtree t) {
  int i;
  for (i=0; t->children[i]; i++) {
    t->numfiles += sumnumfiles(t->children[i]);
  }
  return t->numfiles;
}

/*
 * Add up the total size of files in each subtree
 */
uint64_t sumsize(dirtree t) {
  int i;
  for (i=0; t->children[i]; i++) {
    t->totsize += sumsize(t->children[i]);
  }
  return t->totsize;
}

void sumUp(MYSQL *db, dirtree t) {
  MYSQL_RES *res = NULL;
  MYSQL_ROW row;
  // This will count the number of copies of each file and
  // store it in the "copies" value in the file table
  if (verbose) {
    fprintf(stderr, "Counting duplicates.\n");
  }
  mysql_query(db, COUNTDUPS);

  // This will sum the number of files in each directory
  // that have more than one copy.
  if (verbose) {
    fprintf(stderr, "Summing up, first step.\n");
  }
  mysql_query(db, SUMSTEP1);

  // Now we read in the number of duplicates from the database
  // and update our internal structure
  if (verbose) {
    fprintf(stderr, "Summing up, second step.\n");
  }
  mysql_query(db, READDIRS);
  if ((res = mysql_store_result(db)) == NULL) {
    fprintf(stderr, "mysql_store_result(): %s", mysql_error(db));
    exit(-201);
  }
  if (mysql_num_fields(res) != 2 || mysql_num_rows(res) < 1) {
    fprintf(stderr, "Failed.\n");
    exit(-202);
  }
  while ((row = mysql_fetch_row(res))) {
    setnumdups1(t, atol(row[0]), atol(row[1]));
  }
  mysql_free_result(res);

  // Finally, we add up all the duplicates in memory, and save them
  // to the database at the same time
  if (verbose) {
    fprintf(stderr, "Summing up, third step.\n");
  }
  sumnumdups(db, t);
}

/*
 * Add a directory to the correct parent. If the parent
 * doesn't exist in the tree, this will ultimately return 0.
 */
int adddir(dirtree *t, long id, long parentid, unsigned long numfiles, uint64_t totsize, unsigned long numdups) {
  int i;
  if ((*t)->id == parentid) {
    dirtree child = newDirinfo();
    child->id = id;
    child->numfiles = numfiles;
    child->totsize = totsize;
    child->numdups = numdups;
    addChild(*t, child);
    return 1;
  } else {
    for (i=0; (*t)->children[i]; i++) {
      if (adddir(&((*t)->children[i]), id, parentid, numfiles, totsize, numdups)) {
	return 1;
      }
    }
  }
  return 0;
}

void readdirsfromdb(MYSQL *db, dirtree *t) {
  MYSQL_RES *res = NULL;
  MYSQL_ROW row;
  long id, parentid;
  unsigned long numfiles, numdups;
  uint64_t totsize;
  if (verbose) {
    fprintf(stderr, "Counting files and dups, and adding up sizes, step 1.\n");
  }
  mysql_query(db, RECOUNTDIR);
  if (verbose) {
    fprintf(stderr, "Reading directory tree from database.\n");
  }
  mysql_query(db, READDIRSALL);
  if ((res = mysql_store_result(db)) == NULL) {
    fprintf(stderr, "mysql_store_result(): %s", mysql_error(db));
    exit(-201);
  }
  if (mysql_num_fields(res) != 5 || mysql_num_rows(res) < 1) {
    fprintf(stderr, "Failed.\n");
    exit(-202);
  }
  while ((row = mysql_fetch_row(res))) {
    id=atol(row[0]);
    parentid=atol(row[1]);
    numfiles=row[2]?atol(row[2]):0;
    totsize=row[3]?atol(row[3]):0;
    numdups=row[4]?atol(row[4]):0;
    if (!adddir(t, id, parentid, numfiles, totsize, numdups)) {
      fprintf(stderr, "Failed to build tree: insert id %ld failed.\n", atol(row[0]));
      exit(-301);
    }
  }
  mysql_free_result(res);
  if (verbose) {
    fprintf(stderr, "Sum up number of files, step 2.\n");
  }
  sumnumfiles(*t);
  if (verbose) {
    fprintf(stderr, "Adding up sizes, step 2.\n");
  }
  sumsize(*t);
}

int main(int argc, char *argv[]) {
  MYSQL db;
  dirtree t = NULL;
  int i;
  int rehash=1;
  char db_host[64];
  char db_user[16];
  char db_password[128];
  char db_db[16];
  char *conffile=NULL;
  char *instance=NULL;

  db_host[0] = '\0';
  db_user[0] = '\0';
  db_password[0] = '\0';
  db_db[0] = '\0';
  
  int o;

  while ((o = getopt(argc, argv, "c:i:ve"))!=-1) {
    switch (o) {
    case 'c':
      conffile = optarg;
      break;
    case 'i':
      instance = optarg;
      break;
    case 'v':
      verbose++;
      break;
    case 'e':
      rehash = 0;
      break;
    default:
      usage(argv[0]);
      return -1;
      break;
    }
  }

  if (conffile && instance) {
    conf_getvar(conffile, "database", instance, "db_host", db_host, sizeof(db_host));
    conf_getvar(conffile, "database", instance, "db_user", db_user, sizeof(db_user));
    conf_getvar(conffile, "database", instance, "db_password", db_password, sizeof(db_password));
    conf_getvar(conffile, "database", instance, "db_db", db_db, sizeof(db_db));
  }

  if (!strlen(db_host) || !strlen(db_user) || !strlen(db_password) || !strlen(db_db)) {
    usage(argv[0]);
    return -1;
  }
    
  if (!(mysql_init(&db))) {
   fprintf(stderr, "mysql_init(): %s", mysql_error(&db));
    return -21;
  }

  if (!(mysql_real_connect(&db, db_host, db_user, db_password, db_db, 0, NULL, 0))) {
   fprintf(stderr, "mysql_real_connect(): %s", mysql_error(&db));
    return -22;
  }

  t = newDirinfo();
  t->id = -1;

  if (rehash) {
    if (verbose) {
      fprintf(stderr, "Emptying file table\n");
    }
    mysql_query(&db, EMPTYDIR);
    if (verbose) {
      fprintf(stderr, "Emptying directory table\n");
    }
    mysql_query(&db, EMPTYFILE);
    
    for (i=optind; i<argc; i++) {
      dirtree child=NULL;
      while (strlen(argv[i]) > 1 && argv[i][strlen(argv[i]) - 1] == '/') {
	argv[i][strlen(argv[i]) - 1] = '\0';
      }
      if (verbose) {
	fprintf(stderr, "Traversing %s and calculating hashes.\n", argv[i]);
      }
      calcdir(&db, &child, argv[i], -1);
      addChild(t, child);
    }
  } else {
    readdirsfromdb(&db, &t);
  }

  sumUp(&db, t);
  if (verbose) {
    fprintf(stderr, "Done. Cleaning up.\n");
  }
  mysql_close(&db);
  freetree(&t);
  return 0;
}
