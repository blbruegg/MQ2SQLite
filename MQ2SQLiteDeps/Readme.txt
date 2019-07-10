Current Version is: 3.28 (2019-04-16)

== Updating ==
Download the latest amalgamation source version of SQLite: https://www.sqlite.org/download.html
Extract the files to a working directory
Run Developer Command Prompt for VS 20xx
Navigate to the the working directory
Compile: 
	cl /c /EHsc sqlite3.c
Create static library: 
	lib sqlite3.obj
Copy sqlite3.h and sqlite3.lib to MQ2SQLiteDeps