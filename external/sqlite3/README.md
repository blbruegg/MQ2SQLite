# SQLite 3 lib

Current Version is: 3.33.0

Last Reviewed:  2020-09-15

## Updating

1. Download the latest amalgamation source version of SQLite: https://www.sqlite.org/download.html
2. Extract the files to external/sqlite3/src
3. Compile solution

### Updating from Command Line

1. Run Developer Command Prompt for VS 20xx
2. Navigate to external/sqlite
3. Compile:
   cl /c /EHsc sqlite3.c
4. Create static library:
    lib sqlite3.obj

## Compile Time Options

Compile time options are here:  https://www.sqlite.org/compile.html