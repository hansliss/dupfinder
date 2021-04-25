# dupfinder
Populate a database with enough information to make it possible to easily find duplicate directory trees and files.

Create a MySQL/MariaDB database with the tables from createdb.sql, and change the credentials hardcoded
in the source to the correct ones.

Build 'dupfinder'.
Run dupfinder with one or more directory paths as arguments, and let it calculate hashes of all the files,
and sum up the totals into the directory structure. Then you can use fairly simple SQL queries to find info
about duplicates. A few examples:

Find directories with 100% duplicates in them, sorted by total size:
```
select *,100*numdups/numfiles from directory where numdups=numfiles order by totsize desc limit 20;
```

Find directories with 100% duplicates in them, sorted by total number of files:
```
select *,100*numdups/numfiles from directory where numdups=numfiles order by numfiles desc limit 20;
```

Find the files that take up the most unnecessary space:
```
select * from file join directory on file.parentid = directory.id order by copies*size desc limit 20;
```
