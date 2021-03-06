CREATE TABLE directory (
       id INT NOT NULL AUTO_INCREMENT,
       parentid INT,
       path VARCHAR(4096),
       numfiles INT UNSIGNED DEFAULT 0,
       totsize BIGINT UNSIGNED DEFAULT 0,
       numdups INT UNSIGNED DEFAULT 0,
       PRIMARY KEY(id)
);

CREATE TABLE file (
       id INT AUTO_INCREMENT,
       name VARCHAR(4096),
       parentid INT,
       size BIGINT UNSIGNED,
       copies INT UNSIGNED,
       hash VARCHAR(32),
       PRIMARY KEY(id),
       INDEX h (hash),
       INDEX pi (parentid),
       INDEX c (copies)
);
