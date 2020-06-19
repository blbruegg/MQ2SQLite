# MQ2SQLite

This plugin allows you to interact with a SQLite database.  A SQLite database is just
a file that can be accessed using SQL commands.  This plugin gives you access to the
SQLite API and adds a TLO for viewing the results.

Since the results of the query are stored in memory, this can take up a lot of memory
if you use unique query names and do not clear your results from memory.  So, don't do
that.  Be sure to use the `/sqlite clear <ResultName>` function when you're done accessing
the results.  Otherwise you can use the same name for your query each time which will clear
the results for you before each run.

## Simple Usage

```/sqlite query <"Path to Database File"> <QueryName> QUERY```
Example:  
```/sqlite query C:\Test.db myquery SELECT * FROM Table;```

## Available TLOs:

  `${sqlite.status[QueryName]}` -- String - Current status - Either Active, Success, or Failed  
  `${sqlite.rows[QueryName]}` -- Int - The number of rows returned for results  
  `${sqlite.result[QueryName Row ColumnName]}` -- String containing results (or Failed)  
  `${sqlite.resultcode[QueryName]}` -- Int - Custom SQLite ResultCode (see below)  

## Result Codes:

Result codes are returned from the SQLite Standard Result Codes found here:
https://www.sqlite.org/c3ref/c_abort.html

Additionally, custom errors are returned as negative numbers.  The following return codes may be returned:  
  -1: The query is still running (Active)  
  -2: The query you were looking for was not found (perhaps the name is wrong?)  
  -3: There was a conversion error on string to int conversion for the result code

Example:  
  `/echo ${sqlite.result[myquery 1 Name]}`
  The above would return the value of the column named "Name" for the first row of results from myquery.

## Advanced Usage

By default, the `/sqlite query` command will open a database, run the query and then close the database.
This doesn't allow for more complex operations like transactions or in-memory tables since the connection
closes after every operation.  It does have the benefit of handling all of the operations for you, however.

When your needs are a bit more robust, you can use the advanced query syntax to perform any operation you
would like.  This also allows for passing flags to the database open operation.

Opening a database connection:  
```/sqlite open <ConnectionName> <"Path to Database File"> [FLAGS]```
Example:  
```/sqlite open MyDatabase C:\Test.db SQLITE_OPEN_READWRITE|SQLITE_OPEN_WAL```

This would open the database C:\Test.db in read write mode with write ahead logging enabled.  For a list of
available flags see:  https://www.sqlite.org/c3ref/c_open_autoproxy.html

Once the connection is open, you can use advquery to query that specific connection:
```/sqlite advquery <ConnName> <ResultName> <QUERY>```
Example:  
```/sqlite advquery MyDatabase myquery SELECT * FROM Table;```

This performs the query on the connection named MyDatabase and stores the result in myquery.  See above for
how to access this result.

Finally, when you are done, you will want to close the connection to the database:
```/sqlite close <ConnName>```
Example:  
```/sqlite close MyDatabase```

It should go without saying that advanced usage can be dangerous and I leave it in your hands not to screw
it up.  But, in case it doesn't go without saying: Advanced usage can be dangerous and I leave it in your
hands not to screw it up.