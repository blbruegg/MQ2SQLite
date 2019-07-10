// MQ2SQLite.cpp : Defines the entry point for the DLL application.
//

// This plugin allows you to interact with a SQLite database.  A SQLite database is just a file that
// can be accessed using SQL commands.  This plugin gives you access to the SQLite API and adds
// a TLO for viewing the results.

// Since the results of the query are stored in memory, this can take up a lot of memory if you use
// unique query names and do not clear your results from memory.  So, don't do that.  Be sure to use
// the .clear function when you're done accessing the results or continue to use the same name for
// your query which will clear the results for you before each run.


#include "../MQ2Plugin.h"
#include "..\MQ2SQLiteDeps\sqlite3.h"
#include <fstream>
#include <experimental/filesystem>

#pragma comment(lib, "..\\MQ2SQLiteDeps\\sqlite3.lib")

PreSetup("MQ2SQLite");

// Setup our namespace
namespace KnightlySQLite {
	bool boolDebug = FALSE;
	// A Map of a Map of a Map of a String?  What the hell?  Is this some Dora the Explorer Bullshit?
	// Well, we want the associative reference to be like this:
	//        Query Name   Row  Column Name
	// Result["QueryName"]["1"]["ColName"]
	// And we don't know what kind of data it's going to be so we're going to treat it like a string.
	std::map<std::string, std::map<std::string, std::map<std::string, std::string>>> multimapSQLResult;

	// Log Functions we'll be using
	class Log {
		public:
			// Message is for logging a standard message.
			// All other logging calls go through this base.
			static void Message(std::string strMessage) {
				CHAR pcharMessage[MAX_STRING];
				strMessage = "\ay[\agMQ2SQLite\ay]\aw ::: \ao" + strMessage;
				strcpy_s(pcharMessage, strMessage.c_str());
				WriteChatf(pcharMessage);
			}

			// Error is for logging errors
			static void Error(std::string strError) {
				strError = "\arERROR: " + strError;
				Message(strError);
			}

			// Debug is for logging debug messages and only
			// works if boolDebug is TRUE.
			static void Debug(std::string strDebug) {
				strDebug = "\amDEBUG: " + strDebug;
				if (boolDebug) {
					Message(strDebug);
				}
			}

			static void ShowHelp() {
				Message("\ayUsage:");
				Message("\ay     /sqlite <\"Path to Database File\"> <QueryName> QUERY");
				Message("\ayExample:");
				Message("\ay     /sqlite \"C:\\Test.db\" myquery SELECT * FROM Table;");
				Message(" ");
				Message("\ayAvailable TLOs:");
				Message("\ay     ${sqlite.status[QueryName]} -- String - Current status - Either Active, Success, or Failed");
				Message("\ay     ${sqlite.rows[QueryName]} -- Int - The number of rows returned for results");
				Message("\ay     ${sqlite.clear[QueryName]} -- Clears memory of the query results");
				Message("\ay     ${sqlite.result[QueryName Row ColumnName]} -- String containing results (or Failed)");
				Message("\ay     ${sqlite.resultcode[QueryName]} -- Int - SQLite ResultCode (or -1: Active, -2: Query Not Found)");
				Message("\ayExample:");
				Message("\ay     /echo ${sqlite.result[myquery 1 Name]}");
				Message("\ayThe above would return the value of the column named \"Name\" for the first row of results from myquery.");
			}
	};

	class File {
		public:
			static std::string pathValidator(std::string strFilePath) {
				// Remove quotes from the file path
				strFilePath.erase(remove(strFilePath.begin(), strFilePath.end(), '\"'), strFilePath.end());
				// If the path is relative, change the directory it's relative TO.
				std::experimental::filesystem::path path(strFilePath);
				if (path.is_relative()) {
					// Tack on the base MQ2 directory
					strFilePath = std::string(gszINIPath) + "\\" + strFilePath;
				}
				return strFilePath;
			}

			static bool IsValidFilePath(std::string strFilePath) {
				// Remove quotes from the file path
				strFilePath = pathValidator(strFilePath);
				// Check if the file already exists (if it does, we know it's valid)
				std::ifstream readPath(strFilePath);
				if (!readPath)
				{
					// If the file doesn't already exist then it doesn't necessarily mean it can't be created
					std::ofstream writePath(strFilePath);
					if (!writePath)
					{
						return FALSE;
					}
					else {
						// Close any open write handles
						writePath.close();
						return TRUE;
					}
				}
				else {
					// Close any open read handles
					readPath.close();
					return TRUE;
				}
			}
	};

	class SQL {
		public:
			// Recursively Clear the map of results
			static bool ClearQueryResults(std::string QueryName) {
				if (KnightlySQLite::multimapSQLResult.count(QueryName) == 1) {
					if (KnightlySQLite::multimapSQLResult[QueryName].count("Metadata") == 1) {
						if (KnightlySQLite::multimapSQLResult[QueryName]["Metadata"].count("Rows") == 1) {
							int intNumRows = std::stoi(KnightlySQLite::multimapSQLResult[QueryName]["Metadata"]["Rows"]);
							if (intNumRows > 0) {
								int i = 1;
								for (i = 1; i <= intNumRows; ++i) {
									if (KnightlySQLite::multimapSQLResult[QueryName].count(std::to_string(i)) == 1) {
										KnightlySQLite::multimapSQLResult[QueryName][std::to_string(i)].clear();
									} else {
										KnightlySQLite::Log::Debug("Cannot clear, rows does not exist for " + QueryName + "[" + std::to_string(i) + "]");
									}
								}
							}
							KnightlySQLite::multimapSQLResult[QueryName]["Metadata"].clear();
							KnightlySQLite::multimapSQLResult[QueryName].clear();
							KnightlySQLite::multimapSQLResult.erase(QueryName);
							if (KnightlySQLite::multimapSQLResult.count(QueryName) == 1) {
								KnightlySQLite::Log::Debug("Something went wrong, still exists: " + QueryName);
								return FALSE;
							}
						} else {
							KnightlySQLite::Log::Debug("Cannot clear, Rows node does not exist for " + QueryName);
						}
					} else {
						KnightlySQLite::Log::Debug("Cannot clear, Metadata does not exist for " + QueryName);
					}
				} else {
					KnightlySQLite::Log::Debug("Nothing to clear, query does not exist: " + QueryName);
				}
				return TRUE;
			}

			// Callback for SQL Results
			// *data = Data provided in the 4th argument of sqlite3_exec()
			//  argc = The number of columns in row
			//  argv = An array of strings representing fields in the row
			// azColName = An array of strings representing column names
			static int callbackSQLite(void *data, int argc, char **argv, char **azColName) {
				std::string strQueryName = std::string((const char*)data);
				int i;
				
				if (KnightlySQLite::multimapSQLResult[strQueryName]["Metadata"].count("Rows") == 1) {
					// Convert the current rows to an int, add one, convert it back
					KnightlySQLite::multimapSQLResult[strQueryName]["Metadata"]["Rows"] = std::to_string((std::stoi(KnightlySQLite::multimapSQLResult[strQueryName]["Metadata"]["Rows"])+1));
				} else {
					// We are the first row.
					KnightlySQLite::multimapSQLResult[strQueryName]["Metadata"]["Rows"] = "1";
				}				

				for (i = 0; i < argc; i++) {
					std::string strColumnName = azColName[i];
					std::string strColumnValue = argv[i] ? argv[i] : "NULL";
					KnightlySQLite::multimapSQLResult[strQueryName][(KnightlySQLite::multimapSQLResult[strQueryName]["Metadata"]["Rows"])][strColumnName] = strColumnValue;
				}

				return 0;
			}
	};
}


// Define the SQLite Command
PLUGIN_API VOID SQLiteCommand(PSPAWNINFO pSpawn, PCHAR szLine)
{
	CHAR szParam1[MAX_STRING] = { 0 };
	CHAR szParam2[MAX_STRING] = { 0 };
	CHAR szParam3[MAX_STRING] = { 0 };
	std::string strLine = szLine;
	std::string strSQLCommand;
	GetArg(szParam1, szLine, 1, 1);
	GetArg(szParam2, szLine, 2, 1);
	GetArg(szParam3, szLine, 3);
	// If the first parameter is "help" or empty then show the help info
	if (szParam1 && (!strcmp(szParam1, "help") || strlen(szParam1) == 0)) {
		KnightlySQLite::Log::ShowHelp();
	} else {
		// Check to make sure we have at least 3 parameters (if not we don't have enough to process a query)
		if (szParam3 && strlen(szParam3) > 0) {
			// Validate that the first argument is a valid file path.
			if (KnightlySQLite::File::IsValidFilePath(szParam1)) {
				// Validate that the second argument is a valid query name?  Nah, let 'em crash.

				// Connect to the Database
				sqlite3 *db;
				char *zErrMsg = 0;
				int rc;

				rc = sqlite3_open(KnightlySQLite::File::pathValidator(szParam1).c_str(), &db);

				if (rc) {
					KnightlySQLite::Log::Error("Can't open database (" + std::string(szParam1) + ") : " + std::string(sqlite3_errmsg(db)));
				} else {
					// We got a connection, clear the Query to prep for a new query
					if (KnightlySQLite::SQL::ClearQueryResults(szParam2)) {
						// Get the SQL Command that was sent
						strSQLCommand = strLine.substr((std::string(szParam1) + " " + std::string(szParam2) + " ").length());
						// Set the status to Active
						KnightlySQLite::multimapSQLResult[szParam2]["Metadata"]["Status"] = "Active";
						// Set the number of rows to 0
						KnightlySQLite::multimapSQLResult[szParam2]["Metadata"]["Rows"] = "0";
						// In the event of a long-running query store the result code of "-1" to mean Active
						KnightlySQLite::multimapSQLResult[szParam2]["Metadata"]["ResultCode"] = "-1";
						// Execute the SQL Command
						rc = sqlite3_exec(db, strSQLCommand.c_str(), KnightlySQLite::SQL::callbackSQLite, szParam2, &zErrMsg);
						// Store the result code
						KnightlySQLite::multimapSQLResult[szParam2]["Metadata"]["ResultCode"] = std::to_string(rc);
						if (rc != SQLITE_OK) {
							KnightlySQLite::Log::Debug("Query '" + std::string(szParam2) + "' Failed: " + std::string(zErrMsg));
							// Set the status to failed
							KnightlySQLite::multimapSQLResult[szParam2]["Metadata"]["Status"] = "Failed: " + std::string(zErrMsg);
							sqlite3_free(zErrMsg);
						}
						else {
							KnightlySQLite::Log::Debug("Query '" + std::string(szParam2) + "' Succeeded!");
							// Set the status to success
							KnightlySQLite::multimapSQLResult[szParam2]["Metadata"]["Status"] = "Success";
						}
					} else {
						KnightlySQLite::Log::Error("Could not clear for Query:" + std::string(szParam2));
					}
					sqlite3_close(db);
				}
			} else {
				KnightlySQLite::Log::Error("Invalid File Path: " + std::string(szParam1));
			}
		} else {
			KnightlySQLite::Log::Error("Missing parameters.");
			KnightlySQLite::Log::ShowHelp();
		}
	}
}

class MQ2SQLiteType *pSQLiteType = nullptr;
class MQ2SQLiteType : public MQ2Type {
	private:
		CHAR _szBuffer[MAX_STRING];
	public:
		enum Members {
			Status,
			status,
			Rows,
			rows,
			Result,
			result,
			ResultCode,
			Resultcode,
			resultcode,
			Clear,
			clear
		};

		MQ2SQLiteType() : MQ2Type("SQLite") {
			TypeMember(Status);
			TypeMember(status);
			TypeMember(Rows);
			TypeMember(rows);
			TypeMember(Result);
			TypeMember(result);
			TypeMember(ResultCode);
			TypeMember(Resultcode);
			TypeMember(resultcode);
			TypeMember(Clear);
			TypeMember(clear);
		}

		bool GetMember(MQ2VARPTR VarPtr, char* Member, char* Index, MQ2TYPEVAR &Dest) {
			_szBuffer[0] = '\0';
			// Query Name
			CHAR szResultParam1[MAX_STRING] = { 0 };
			// Row Number
			CHAR szResultParam2[MAX_STRING] = { 0 };
			// Column Name
			CHAR szResultParam3[MAX_STRING] = { 0 };

			PMQ2TYPEMEMBER pMember = MQ2SQLiteType::FindMember(Member);
			if (!pMember) return FALSE;
			switch ((Members)pMember->ID) {
				case Status:
				case status:
					Dest.Type = pStringType;
					// If we have a status set ...
					if (KnightlySQLite::multimapSQLResult[Index]["Metadata"].count("Status") == 1)
					{
						strcpy_s(_szBuffer, KnightlySQLite::multimapSQLResult[Index]["Metadata"]["Status"].c_str());
						Dest.Ptr = &_szBuffer[0];
						return TRUE;
					} else {
						// If we don't have a status, just return null...
						strcpy_s(_szBuffer, "NULL");
						Dest.Ptr = &_szBuffer[0];
						return TRUE;
					}
				case Rows:
				case rows:
					Dest.Type = pIntType;
					if (KnightlySQLite::multimapSQLResult[Index]["Metadata"].count("Rows") == 1)
					{
						Dest.Int = std::stoi(KnightlySQLite::multimapSQLResult[Index]["Metadata"]["Rows"]);
					} else {
						Dest.Int = 0;
					}
					return TRUE;
				case Result:
				case result:
					Dest.Type = pStringType;
					// Get Query Name
					GetArg(szResultParam1, Index, 1, 1);
					GetArg(szResultParam2, Index, 2);
					GetArg(szResultParam3, Index, 3, 1);
					// Make sure we have three parameters
					if (szResultParam3 && strlen(szResultParam3) > 0) {
						// Make sure we have that Query
						if (KnightlySQLite::multimapSQLResult.count(szResultParam1) == 1) {
							// Make sure the query is complete
							if (KnightlySQLite::multimapSQLResult[szResultParam1]["Metadata"]["Status"] == "Success") {
								// Make sure the row exists
								if (KnightlySQLite::multimapSQLResult[szResultParam1].count(szResultParam2)) {
									// Make sure the column exists
									if (KnightlySQLite::multimapSQLResult[szResultParam1][szResultParam2].count(szResultParam3)) {
										// Return whatever they asked for
										strcpy_s(_szBuffer, (KnightlySQLite::multimapSQLResult[szResultParam1][szResultParam2][szResultParam3]).c_str());
									} else {
										KnightlySQLite::Log::Error("Column " + std::string(szResultParam3) + " does not exist.");
										strcpy_s(_szBuffer, ("Failure:  Column: " + std::string(szResultParam3) + " does not exist.").c_str());
									}
								} else {
									KnightlySQLite::Log::Error("Row " + std::string(szResultParam2) + " does not exist.");
									strcpy_s(_szBuffer, ("Failure:  Row: " + std::string(szResultParam2) + " does not exist.").c_str());
								}
							} else {
								strcpy_s(_szBuffer, ("Failure:  Cannot get results, status is: " + KnightlySQLite::multimapSQLResult[szResultParam1]["Metadata"]["Status"]).c_str());
							}
						} else {
							KnightlySQLite::Log::Error("Query " + std::string(szResultParam1) + " does not exist.");
							strcpy_s(_szBuffer, ("Failure:  " + std::string(szResultParam1) + " does not exist.").c_str());
						}
					} else {
						KnightlySQLite::Log::Error("Result Call requires three parameters ${sqlite.result[QueryName RowNumber ColumnName]}.");
						strcpy_s(_szBuffer, "Failure:  Need 3 parameters");
					}
					Dest.Ptr = &_szBuffer[0];
					return TRUE;
				case ResultCode:
				case Resultcode:
				case resultcode:
					Dest.Type = pIntType;
					// If we have a ResultCode
					if (KnightlySQLite::multimapSQLResult[Index]["Metadata"].count("ResultCode") == 1)
					{
						Dest.Int = std::stoi(KnightlySQLite::multimapSQLResult[Index]["Metadata"]["ResultCode"]);
					}
					else {
						// Set the return to -2 for "Query Not Found"
						Dest.Int = -2;
					}
					return TRUE;
				case Clear:
				case clear:
					Dest.Type = pIntType;
					Dest.Int = KnightlySQLite::SQL::ClearQueryResults(Index);
					return TRUE;
			}
			return FALSE;
		}

		bool FromData(MQ2VARPTR &VarPtr, MQ2TYPEVAR &Source) { return FALSE; }
		bool FromString(MQ2VARPTR &VarPtr, char* Source) { return FALSE; }
};

BOOL SQLiteData(PCHAR szIndex, MQ2TYPEVAR &Dest)
{
	Dest.DWord = 1;
	Dest.Type = pSQLiteType;
	return TRUE;
}

// Called once, when the plugin is to initialize
PLUGIN_API VOID InitializePlugin(VOID)
{
    DebugSpewAlways("Initializing MQ2SQLite");
	// Add /sqlite
	AddCommand("/sqlite", SQLiteCommand);
	// Add a data type to handle results
	pSQLiteType = new MQ2SQLiteType;
	AddMQ2Data("sqlite", SQLiteData);
}

// Called once, when the plugin is to shutdown
PLUGIN_API VOID ShutdownPlugin(VOID)
{
    DebugSpewAlways("Shutting down MQ2SQLite");
	// Remove /sqlite
	RemoveCommand("/sqlite");
	// Remove data type
	RemoveMQ2Data("sqlite");
	delete pSQLiteType;
}