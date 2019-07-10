# MQ2SQLite

This plugin allows you to interact with a SQLite database.  A SQLite database is just
a file that can be accessed using SQL commands.  This plugin gives you access to the
SQLite API and adds a TLO for viewing the results.

Since the results of the query are stored in memory, this can take up a lot of memory
if you use unique query names and do not clear your results from memory.  So, don't do
that.  Be sure to use the `.clear` function when you're done accessing the results or
continue to use the same name for your query which will clear the results for you 
before each run.

Usage:  
```/sqlite <"Path to Database File"> <QueryName> QUERY```  
Example:  
```/sqlite C:\Test.db myquery SELECT * FROM Table;```

Available TLOs:  
  `${sqlite.status[QueryName]}` -- String - Current status - Either Active, Success, or Failed  
  `${sqlite.rows[QueryName]}` -- Int - The number of rows returned for results  
  `${sqlite.clear[QueryName]}` -- Clears memory of the query results  
  `${sqlite.result[QueryName Row ColumnName]}` -- String containing results (or Failed)  
  `${sqlite.resultcode[QueryName]}` -- Int - SQLite ResultCode (or -1: Active, -2: No Query Found)  SQLite Result Codes can be found here: https://www.sqlite.org/c3ref/c_abort.html

Example:  
  `/echo ${sqlite.result[myquery 1 Name]}`  
  The above would return the value of the column named "Name" for the first row of results from myquery.