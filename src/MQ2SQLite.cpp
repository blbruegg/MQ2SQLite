// MQ2SQLite.cpp : Defines the entry point for the DLL application.
//

// This plugin allows you to interact with a SQLite database.  A SQLite database is just a file that
// can be accessed using SQL commands.  This plugin gives you access to the SQLite API and adds
// a TLO for viewing the results.

// Since the results of the query are stored in memory, this can take up a lot of memory if you use
// unique query names and do not clear your results from memory.  So, don't do that.  Be sure to use
// the clear function when you're done accessing the results or continue to use the same name for
// your query which will clear the results for you before each run.


#include "../../MQ2Plugin.h"
#include "sqlite3.h"

#include <fstream>
#define _SILENCE_EXPERIMENTAL_FILESYSTEM_DEPRECATION_WARNING
#include <experimental/filesystem>
namespace fs = std::experimental::filesystem;

// MQ2's inclusion of Windows.h interferes with the min/max functions in the std lib.
#undef min

PreSetup("MQ2SQLite");
PLUGIN_VERSION(2020.0915);

namespace KnightlyCommon {
	const bool SHOW_DEBUG_LOGS = false;
	std::string pluginName = "Unnamed Plugin";

	class Log {
		private:
			static std::string GetMacroInfo() {
				std::string strReturn;
				if (PMACROBLOCK pBlock = GetCurrentMacroBlock())
				{
					MACROLINE ml = pBlock->Line.at(gMacroBlock->CurrIndex);
					strReturn = " (" + ml.SourceFile + ":: Line " + std::to_string(ml.LineNumber) + ")";
				}
				return strReturn;
			}
		public:
			// Message is for logging a standard message.
			// All other logging calls go through this base.
			static void Message(std::string strMessage) {
				strMessage = "\ay[\ag" + KnightlyCommon::pluginName + "\ay]\aw ::: \ao" + strMessage;
				// Make sure we don't exceed the limit of WriteChatf -- which is MAX_STRING.
				strMessage = strMessage.substr(0, MAX_STRING - 1);
				// NEXTME: WriteChatf(strMessage.c_str());
				WriteChatf(&strMessage[0]);
			}

			// Warning is for logging warnings
			static void Warning(std::string strWarning) {
				strWarning = "\ayWARNING: " + strWarning + GetMacroInfo();
				Message(strWarning);
			}

			// Error is for logging errors
			static void Error(std::string strError) {
				strError = "\arERROR: " + strError + GetMacroInfo();
				Message(strError);
			}

			// Debug is for logging debug messages and only
			// works if SHOW_DEBUG_LOGS is true.
			static void Debug(std::string strDebug) {
				strDebug = "\amDEBUG: " + strDebug;
				if (SHOW_DEBUG_LOGS) {
					Message(strDebug);
				}
			}
	};

	class String {
		public:
			/**
			 * @fn ReplaceSubstring
			 *
			 * @brief Replaces all occurrences of one string with another string
			 *
			 * Starting from the beginning of the string, find each occurrence of a substring
			 * and replace that with another substring.
			 *
			 * @param strOriginal The string that you would like to search
			 * @param strFind The substring to look for in the Original string
			 * @param strReplace The substring to use to replace strFind
			 *
			 * @return std::string The result of replacing the values in the original string
			 */
			static std::string ReplaceSubstring(std::string /* NEXTME: string_view */ strOriginal, std::string /* NEXTME: string_view */ strFind, std::string /* NEXTME: string_view */ strReplace) {
				// Set a tracker for our position
				size_t iCurrentPosition = 0;
				std::string strReturn = strOriginal.data();

				while (iCurrentPosition != std::string::npos) {
					// Find the next occurence of the substring
					iCurrentPosition = strReturn.find(strFind, iCurrentPosition);
					// If we found something, then we need to process the change
					if (!(iCurrentPosition == std::string::npos)) {
						// Replace the string we found
						strReturn.replace(iCurrentPosition, strFind.length(), strReplace);
						// Move our tracker past the string we just replaced with so we don't pick it up again
						iCurrentPosition += strReplace.length();
					}
				}
				return strReturn;
			}

			/**
			 *  @fn UnQuoted
			 *
			 *  @brief Unquotes a quoted string, taking into account the escape char
			 *
			 *  Takes a string as input and, assuming it is in the format "quoted" will
			 *  strip the quotes unescape any escaped quotes within the string.
			 *
			 *  If the string is not in quotes, returns the original string.
			 *
			 *  @param strInput The string to unquote
			 *  @param bAllowBothQuoteTypes Default false - if true, allows for single quoted strings
			 *  @param charEscapeQuote Default '\' - the escape character
			 *
			 *  @return std::string The unquoted string.
			 */
			static std::string UnQuoted(std::string /* NEXTME: string_view */ strInput, const bool bAllowBothQuoteTypes = false, const char charEscapeQuote = '\\')
			{
				std::string strReturn = strInput.data();
				const char charQuote = strInput[0];

				// For this to be a quoted string, it must be at least 2 characters (for null)
				if (strReturn.length() > 1) {
					// If this is a double quote or it's a single quote and we allow both quote types, then we have work to do.
					if (charQuote == '"' || charQuote == '\'' && bAllowBothQuoteTypes) {
						// If the last character is a quote
						if (strReturn[strReturn.length() - 1] == charQuote) {
							strReturn = strReturn.substr(1, strReturn.length() - 2);
							strReturn = ReplaceSubstring(strReturn, std::string(1, charEscapeQuote) + std::string(1, charQuote), std::string(1, charQuote));
						}
					}
				}

				return strReturn;
			}

			/**
			 * @fn GetArgsFromString
			 *
			 * @brief Splits a string by a delimiter, adds them to a vector, returns the vector
			 *
			 * Walks through all of the characters of a string.  When it comes to the specified
			 * delimiter, splits whatever has been found so far and pushes it to the vector. The
			 * delimiter cannot be escaped, but if it is contained in quotes it will be treated
			 * as if it is part of the quoted string instead of as a delimiter.
			 *
			 * Quotes can be escaped so that they can be contained within a quoted string of the
			 * same quote type.  If they appear outside of a quoted string, they are treated as
			 * part of the string.
			 *
			 * If a match is not found for a quote, the last element in the vector will contain
			 * the remainder of the string.
			 *
			 * @param strInput The string that you would like to split
			 * @param bKeepQuotes If you want the output string to keep the surrounding quotes (if it had them)
			 * @param bKeepEscapeChar If you want to keep the escape character intact when it is escaping a quote
			 * @param bAllowBothQuoteTypes If you want to be able to start a quoted string with ' in addition to "
			 * @param charDelimiter The character to split on (default space)
			 * @param charEscapeQuote The character to use to escape quotes in a quoted string (default \)
			 *
			 * @return std::vector<std::string> A vector containing all of the split strings
			 *
			 **/
			static std::vector<std::string> GetArgsFromString(std::string /* NEXTME: string_view */ strInput, const bool bKeepQuotes = false, const bool bKeepEscapeChar = false, const bool bAllowBothQuoteTypes = false, const char charDelimiter = ' ', const char charEscapeQuote = '\\') {
				// Trim the string
				/* NEXTME:
				strInput.remove_prefix(std::min(strInput.find_first_not_of(" "), strInput.size()));
				strInput.remove_suffix(std::min(strInput.size() - strInput.find_last_not_of(" ") - 1, strInput.size()));
				*/
				strInput.erase(strInput.begin(), std::find_if(strInput.begin(), strInput.end(), [](int ch) { return !std::isspace(ch); }));
				strInput.erase(std::find_if(strInput.rbegin(), strInput.rend(), [](int ch) { return !std::isspace(ch); }).base(), strInput.end());
				// END NEXTME

				std::vector<std::string> result;
				bool inDoubleQuote = false;
				bool inSingleQuote = false;
				std::string currentString;
				for (size_t i = 0; i < strInput.length(); i++) {
					if (strInput[i] == charDelimiter && !(inDoubleQuote || inSingleQuote)) {
						// If the delimiter is a space, we only want to add an argument if
						// our string length is currently greater than zero.  This accounts
						// for a series of spaces beside each other.  For other delimiters,
						// we want to add an argument regardless.  This allows for ,,,,, to skip
						// arguments (but not a series of spaces).
						if (charDelimiter != ' ' || currentString.length() > 0) {
							result.push_back(currentString);
							currentString.clear();
						}
					}
					else if (strInput[i] == charEscapeQuote) {
						// If there is room left in the string
						if (i + 1 < strInput.length()) {
							// Check to see if this is escaping a quote
							if ((inDoubleQuote && strInput[i + 1] == '\"') || (inSingleQuote && strInput[i + 1] == '\'')) {
								if (bKeepEscapeChar) {
									currentString += charEscapeQuote;
								}
								i++;
							}
						}
						currentString += strInput[i];
					}
					else if (bAllowBothQuoteTypes && !inDoubleQuote && strInput[i] == '\'') {
						if (bKeepQuotes) {
							currentString += strInput[i];
						}
						inSingleQuote = !inSingleQuote;
					}
					else if (!inSingleQuote && strInput[i] == '"') {
						if (bKeepQuotes) {
							currentString += strInput[i];
						}
						inDoubleQuote = !inDoubleQuote;
					}
					else {
						currentString += strInput[i];
					}
				}
				// If there's anything left in the string, it's the last argument
				if (currentString.length() > 0) {
					result.push_back(currentString);
				}

				return result;
			}
	};
}

namespace KnightlySQLite {
	// A Map of a Map of a Map of a String?  What the hell?  Is this some Dora the Explorer Bullshit?
	// Well, we want the associative reference to be like this:
	//        Query Name   Row  Column Name
	// Result["QueryName"]["1"]["ColName"]
	// And we don't know what kind of data it's going to be so we're going to treat it like a string.
	std::map<std::string, std::map<std::string, std::map<std::string, std::string>>> multimapSQLResult;

	// Store all of our database connections by name
	std::map<std::string, sqlite3*> mapDbConnections;

	// Limit warnings -- deprecate this when their counterparts are
	int warningCountDotClear = 0;
	int warningCountDeprecatedVerb = 0;
	const int MAX_WARNINGS = 5;

	// Log Functions we'll be using
	class Log {
		public:
			static void ShowHelp() {
				KnightlyCommon::Log::Message("\ayUsage:");
				KnightlyCommon::Log::Message("\ay     /sqlite query <\"Path to Database File\"> <ResultName> <QUERY>");
				KnightlyCommon::Log::Message("\ay     /sqlite clear <ResultName>");
				KnightlyCommon::Log::Message("\ay     /sqlite open <ConnName> <\"DB To Open\"> [FLAGS]");
				KnightlyCommon::Log::Message("\ay     /sqlite advquery <ConnName> <ResultName> <QUERY>");
				KnightlyCommon::Log::Message("\ay     /sqlite close <ConnName>");
				KnightlyCommon::Log::Message("\ayAvailable Verbs:");
				KnightlyCommon::Log::Message("\ay     Query - Runs a simple query - opens and closes the database by itself.");
				KnightlyCommon::Log::Message("\ay     Clear - Clear the named query results from memory.");
				KnightlyCommon::Log::Message("\ay     Open - SEE README - Opens a named connection and leaves it open");
				KnightlyCommon::Log::Message("\ay     AdvQuery - SEE README - performs a query on the named open connection");
				KnightlyCommon::Log::Message("\ay     Close - SEE README - Closes the named open connection");
				KnightlyCommon::Log::Message("\ayExample:");
				KnightlyCommon::Log::Message("\ay     /sqlite query \"C:\\Test.db\" myquery SELECT * FROM Table;");
				KnightlyCommon::Log::Message(" ");
				KnightlyCommon::Log::Message("\ayAvailable TLOs:");
				KnightlyCommon::Log::Message("\ay     ${sqlite.status[QueryName]} -- String - Current status - Either Active, Success, or Failed");
				KnightlyCommon::Log::Message("\ay     ${sqlite.connstatus[ConnName]} -- String - Current Connection status - Either Open, or Closed");
				KnightlyCommon::Log::Message("\ay     ${sqlite.rows[QueryName]} -- Int - The number of rows returned for results");
				KnightlyCommon::Log::Message("\ay     ${sqlite.result[QueryName Row ColumnName]} -- String containing results (or Failed)");
				KnightlyCommon::Log::Message("\ay     ${sqlite.resultcode[QueryName]} -- Int - Custom SQLite ResultCode (see README)");
				KnightlyCommon::Log::Message("\ayExample:");
				KnightlyCommon::Log::Message("\ay     /echo ${sqlite.result[myquery 1 Name]}");
				KnightlyCommon::Log::Message("\ayThe above would return the value of the column named \"Name\" for the first row of results from myquery.");
			}
	};

	class File {
		public:
			/**
			 * @fn MakeAbsolutePath
			 *
			 * @brief Takes input of a path and outputs the path relative to MQ2
			 *
			 * Takes a file path as input, determines if the path is relative and
			 * if it IS relative, makes it relative to the MQ2 directory, rather
			 * than the EQ directory.
			 *
			 * @param pathFile The original file path
			 *
			 * @return std::filesystem::path The absolute path relative to MQ2
			 *
			 **/
			static fs::path MakeAbsolutePath(fs::path pathFile) {
				// If the path is relative, change the directory it's relative TO.
				if (pathFile.is_relative()) {
					// Tack on the base MQ2 directory
					pathFile = gszINIPath / pathFile;
				}
				return pathFile;
			}

			/**
			 * @fn IsValidFilePath
			 *
			 * @brief Returns whether a file path is valid
			 *
			 * Takes a file path as input and runs it through @ref MakeAbsolutePath
			 * to make the path absolute. Then determines if the path is valid
			 * by checking if a file exists (and assuming if it exists we can
			 * write to it) or, if it does not exist, opens a write stream to
			 * see if we can create it.
			 *
			 * @param pathFile The original file path
			 *
			 * @return bool Whether the path is valid or not
			 *
			 **/
			static bool IsValidFilePath(fs::path pathFile) {
				bool bReturn = false;
				// Make this an absolute path
				pathFile = MakeAbsolutePath(pathFile);
				// Check if the file already exists (if it does, we think it's valid)
				if (!fs::exists(pathFile)) {
					// If the file doesn't already exist then it doesn't necessarily mean it can't be created
					std::ofstream writePath(pathFile);
					if (writePath) {
						// Close any open write handles
						writePath.close();
						bReturn = true;
					}
				}
				// The file exists already, which means it's valid
				else {
					bReturn = true;
				}
				return bReturn;
			}
	};

	class SQL {
		public:
			enum custom_sqlite_result
			{
				QUERY_ACTIVE = -1,
				QUERY_NOT_FOUND = -2,
				CONVERSION_ERROR = -3,
				MAP_NOT_ERASED = -10,
				CONN_NOT_FOUND = -11,
				UNKNOWN_ERROR = -12,
				CONN_EXISTS = -13,
			};

			/**
			 * @fn OpenDatabase
			 *
			 * @brief Opens a SQLite Database Connection
			 *
			 * Opens a SQLite Database Connection, with optional flags that
			 * get passed in via string.  Stores the connection information
			 * in a map.
			 *
			 * Returns the result code of opening the database.
			 *
			 * @param strConnName What to name the connection for later reference
			 * @param pathDatabase The path to the database
			 * @param strFlags The string version of the SQLite flags ref: https://www.sqlite.org/c3ref/c_open_autoproxy.html
			 *
			 * @return int The SQLite Result Code for opening the connection, UNKNOWN_ERROR, or CONN_EXISTS if the connection exists
			 *
			 **/
			static int OpenDatabase(const std::string& strConnName, fs::path pathDatabase, const std::string& /* NEXTME: string_view */ strFlags = "") {
				int iResult = UNKNOWN_ERROR;
				int iOpenFlags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;

				// Sanitize the path if we need to, otherwise take it however the sender passed it
				if(File::IsValidFilePath(pathDatabase)) {
					pathDatabase = File::MakeAbsolutePath(pathDatabase);
				}

				// Note that here we're letting SQLite return the errors if the database can't be opened
				// rather than handling them as an else on the IsValidFilePath.

				// Check to see if the database is already open
				if (KnightlySQLite::mapDbConnections.count(strConnName) != 1) {
					// TODO: Determine if we need to check for null here
					// strFlags !=""
					if (!strFlags.empty()) {
						// The first set of flags, there can be only one.
						if (strFlags.find("SQLITE_OPEN_READONLY") != std::string::npos) {
							iOpenFlags = SQLITE_OPEN_READONLY;
						}
						else if (strFlags.find("SQLITE_OPEN_READWRITE") != std::string::npos) {
							iOpenFlags = SQLITE_OPEN_READWRITE;
						}

						// The rest of the flags we add to the first set:  https://www.sqlite.org/c3ref/c_open_autoproxy.html
						if (strFlags.find("SQLITE_OPEN_URI") != std::string::npos) {
							iOpenFlags |= SQLITE_OPEN_URI;
						}
						if (strFlags.find("SQLITE_OPEN_MEMORY") != std::string::npos) {
							iOpenFlags |= SQLITE_OPEN_MEMORY;
						}
						if (strFlags.find("SQLITE_OPEN_NOMUTEX") != std::string::npos) {
							iOpenFlags |= SQLITE_OPEN_NOMUTEX;
						}
						if (strFlags.find("SQLITE_OPEN_FULLMUTEX") != std::string::npos) {
							iOpenFlags |= SQLITE_OPEN_FULLMUTEX;
						}
						if (strFlags.find("SQLITE_OPEN_SHAREDCACHE") != std::string::npos) {
							iOpenFlags |= SQLITE_OPEN_SHAREDCACHE;
						}
						if (strFlags.find("SQLITE_OPEN_PRIVATEACHE") != std::string::npos) {
							iOpenFlags |= SQLITE_OPEN_PRIVATECACHE;
						}

						if (strFlags.find("SQLITE_OPEN_DELETEONCLOSE") != std::string::npos) {
							iOpenFlags |= SQLITE_OPEN_DELETEONCLOSE;
						}
						if (strFlags.find("SQLITE_OPEN_EXCLUSIVE") != std::string::npos) {
							iOpenFlags |= SQLITE_OPEN_EXCLUSIVE;
						}
						if (strFlags.find("SQLITE_OPEN_AUTOPROXY") != std::string::npos) {
							iOpenFlags |= SQLITE_OPEN_AUTOPROXY;
						}
						if (strFlags.find("SQLITE_OPEN_MAIN_DB") != std::string::npos) {
							iOpenFlags |= SQLITE_OPEN_MAIN_DB;
						}
						if (strFlags.find("SQLITE_OPEN_TEMP_DB") != std::string::npos) {
							iOpenFlags |= SQLITE_OPEN_TEMP_DB;
						}
						if (strFlags.find("SQLITE_OPEN_TRANSIENT_DB") != std::string::npos) {
							iOpenFlags |= SQLITE_OPEN_TRANSIENT_DB;
						}
						if (strFlags.find("SQLITE_OPEN_MAIN_JOURNAL") != std::string::npos) {
							iOpenFlags |= SQLITE_OPEN_MAIN_JOURNAL;
						}
						if (strFlags.find("SQLITE_OPEN_TEMP_JOURNAL") != std::string::npos) {
							iOpenFlags |= SQLITE_OPEN_TEMP_JOURNAL;
						}
						if (strFlags.find("SQLITE_OPEN_SUBJOURNAL") != std::string::npos) {
							iOpenFlags |= SQLITE_OPEN_SUBJOURNAL;
						}
						if (strFlags.find("SQLITE_OPEN_MASTER_JOURNAL") != std::string::npos) {
							iOpenFlags |= SQLITE_OPEN_MASTER_JOURNAL;
						}
						if (strFlags.find("SQLITE_OPEN_WAL") != std::string::npos) {
							iOpenFlags |= SQLITE_OPEN_WAL;
						}
					}

					iResult = sqlite3_open_v2(pathDatabase.string().c_str(), &mapDbConnections[strConnName], iOpenFlags, nullptr);
				}
				// Otherwise the database was already open
				else
				{
					iResult = CONN_EXISTS;
				}
				return iResult;
			}

			/**
			 * @fn CloseDatabase
			 *
			 * @brief Closes an open SQLite Database
			 *
			 * Closes an open SQLite Database and returns the error code.  Erases the map with the
			 * database connection.
			 *
			 * @param strConnName The name of the connection you want to close.
			 *
			 * @return int Returns the SQLite Error Code or CONN_NOT_FOUND (there was no connection) or MAP_NOT_ERASED (couldn't erase the map)
			 *
			 **/
			static int CloseDatabase(const std::string& strConnName) {
				int iResult = CONN_NOT_FOUND;
				if (KnightlySQLite::mapDbConnections.count(strConnName) == 1) {
					iResult = sqlite3_close_v2(KnightlySQLite::mapDbConnections[strConnName]);
					if (KnightlySQLite::mapDbConnections.erase(strConnName) != 1 && iResult == SQLITE_OK) {
						iResult = MAP_NOT_ERASED;
					}
				}
				return iResult;
			}

			/**
			 * @fn GetSQLError
			 *
			 * @brief Gets the string translation of a SQLite Error
			 *
			 * Checks to see if a database mapping exists and if it does
			 * gets whatever the sqlite error message was on that connection.
			 *
			 * @param strConnName The database name to look for
			 *
			 * @return string The error message or that the connection doesn't exist
			 *
			 **/
			static std::string GetSQLError(const std::string& strConnName) {
				std::string strReturn = "Connection Name " + strConnName + " doesn't exist.";
				if (KnightlySQLite::mapDbConnections.count(strConnName) == 1) {
					strReturn = std::string(sqlite3_errmsg(KnightlySQLite::mapDbConnections[strConnName]));
				}
				return strReturn;
			}

			/**
			 * @fn OpenDatabaseWithOutput
			 *
			 * @brief Performs an OpenDatabase call with output to Log Functions
			 *
			 * Attempts to open the specified database and connection.  If the connection is already open
			 * then logs a warning that it was already opened.  If the connection can't be opened then
			 * logs an error with the error message and closes the database.
			 *
			 * @ref OpenDatabase
			 *
			 * @return bool Whether the connection was successful or not
			 *
			 **/
			static bool OpenDatabaseWithOutput(const std::string& strConnName, const fs::path& pathDatabase, const std::string& /* NEXTME: string_view */ strFlags = "") {
				bool bReturn = false;
				const int rc = OpenDatabase(strConnName, pathDatabase, strFlags);

				switch (rc) {
					case UNKNOWN_ERROR:
						KnightlyCommon::Log::Error("Unknown Error with '" + strConnName + "'");
						break;
					case CONN_EXISTS:
						KnightlyCommon::Log::Warning("Database Connection named \"" + strConnName + "\" already open");
						break;
					case SQLITE_OK:
						bReturn = true;
						break;
					default:
						KnightlyCommon::Log::Error("Can't open database (" + pathDatabase.string() + ") : " + GetSQLError(strConnName));
						CloseDatabase(strConnName);
						break;
				}

				return bReturn;
			}

			/**
			 * @fn CloseDatabaseWithOutput
			 *
			 * @brief Performs an CloseDatabase call with output to Log Functions
			 *
			 * Attempts to close the specified connection.  If the connection doesn't exist then logs a
			 * warning.  If the connection can't be closed or cleared from memory, logs the appropriate
			 * error message.
			 *
			 * @ref CloseDatabase
			 *
			 * @return bool Whether the close was successful or not
			 *
			 **/
			static bool CloseDatabaseWithOutput(const std::string& strConnName) {
				bool bReturn = false;
				const int rc = CloseDatabase(strConnName);

				switch (rc) {
					case MAP_NOT_ERASED:
						KnightlyCommon::Log::Error("Couldn't clear \"" + strConnName + "\" from memory.");
					case CONN_NOT_FOUND:
						KnightlyCommon::Log::Warning("Database Connection named \"" + strConnName + "\" doesn't exist to close.");
						break;
					case SQLITE_OK:
						bReturn = true;
						break;
					default:
						KnightlyCommon::Log::Error("Database Connection named \"" + strConnName + "\" couldn't be closed: SQLITE_ERROR: " + std::to_string(rc));
						break;
				}

				return bReturn;
			}

			/**
			 * @fn ClearQueryResults
			 *
			 * @brief Recursively clears the map of Query Results
			 *
			 * Starts at the farthest most point in the map and clears backwards until
			 * it reaches the top of the map.  Returns whether it was successful at
			 * clearing or not.
			 *
			 * It is considered a failure only if the items to clear exist and were not
			 * able to be cleared.  Even if there was nothing to do, it is not a failure.
			 *
			 * @param queryName The name of the Query to Clear
			 *
			 * @return bool True/False for Success/Failure of Clearing
			 *
			 **/
			static bool ClearQueryResults(const std::string &queryName) {
				if (multimapSQLResult.count(queryName) == 1) {
					if (multimapSQLResult[queryName].count("Metadata") == 1) {
						if (multimapSQLResult[queryName]["Metadata"].count("Rows") == 1) {
							// NEXTME: const int intNumRows = GetIntFromString(multimapSQLResult[queryName]["Metadata"]["Rows"], 0);
							const int intNumRows = std::stoi(KnightlySQLite::multimapSQLResult[queryName]["Metadata"]["Rows"]);
							if (intNumRows > 0) {
								for (int i = 1; i <= intNumRows; ++i) {
									if (multimapSQLResult[queryName].count(std::to_string(i)) == 1) {
										multimapSQLResult[queryName][std::to_string(i)].clear();
									} else {
										KnightlyCommon::Log::Debug("Cannot clear, rows does not exist for " + queryName + "[" + std::to_string(i) + "]");
									}
								}
							}
							multimapSQLResult[queryName]["Metadata"].clear();
							multimapSQLResult[queryName].clear();
							multimapSQLResult.erase(queryName);
							if (multimapSQLResult.count(queryName) == 1) {
								KnightlyCommon::Log::Debug("Something went wrong, still exists: " + queryName);
								return false;
							}
						} else {
							KnightlyCommon::Log::Debug("Cannot clear, Rows node does not exist for " + queryName);
						}
					} else {
						KnightlyCommon::Log::Debug("Cannot clear, Metadata does not exist for " + queryName);
					}
				} else {
					KnightlyCommon::Log::Debug("Nothing to clear, query does not exist: " + queryName);
				}
				return true;
			}

			/**
			 * @fn CallbackSQLite
			 *
			 * @brief Callback for SQLite Results
			 *
			 * Accepts the callback from the results of a SQLite Query and stores
			 * the results in the multimap that we have setup.
			 *
			 * @param data Data provided in the 4th argument of sqlite3_exec()
			 * @param argc The number of columns in the row
			 * @param argv An array of strings representing fields in the row
			 * @param azColName An array of strings representing column names
			 *
			 * @return int Always returns 0
			 *
			 **/
			static int CallbackSQLite(void* data, int argc, char** argv, char** azColName) {
				const std::string& strQueryName = *static_cast<std::string*>(data);

				if (KnightlySQLite::multimapSQLResult[strQueryName]["Metadata"].count("Rows") == 1) {
					// Convert the current rows to an int, add one, convert it back (rows are zero if there is a conversion error)
					// NEXTME: KnightlySQLite::multimapSQLResult[strQueryName]["Metadata"]["Rows"] = std::to_string((GetIntFromString(KnightlySQLite::multimapSQLResult[strQueryName]["Metadata"]["Rows"], -1) + 1));
					KnightlySQLite::multimapSQLResult[strQueryName]["Metadata"]["Rows"] = std::to_string((std::stoi(KnightlySQLite::multimapSQLResult[strQueryName]["Metadata"]["Rows"]) + 1));
				}
				else {
					// We are the first row.
					KnightlySQLite::multimapSQLResult[strQueryName]["Metadata"]["Rows"] = "1";
				}

				for (int i = 0; i < argc; i++) {
					std::string strColumnName = azColName[i];
					const std::string strColumnValue = argv[i] ? argv[i] : "NULL";
					KnightlySQLite::multimapSQLResult[strQueryName][(KnightlySQLite::multimapSQLResult[strQueryName]["Metadata"]["Rows"])][strColumnName] = strColumnValue;
				}

				return 0;
			}

			/**
			 * @fn QueryDatabase
			 *
			 * @brief Queries an existing SQLite database
			 *
			 * Clears any existing results from the multimap and then performs a query
			 * on an existing SQLite database.  Regardless of the query's success, stores
			 * the result in the multimap.  Extends the result codes to include checks
			 * specific to the SQLite plugin.  See return below for more info.
			 *
			 * @param strConnName The key for the map that holds the connection information
			 * @param strQueryName The key to store the results in
			 * @param strQueryStatement The query to run
			 *
			 * @return int The result code of the query.  Result codes are:
			 *             CONN_NOT_FOUND: Connection with that name does not exist.
			 *             MAP_NOT_ERASED: Query Results could not be cleared (thus no room for storage)
			 *             QUERY_ACTIVE: Query is Active (should never be returned by this function)
			 *             SQLITE_OK: Success
			 *             1+: The SQLITE error code from the query.
			 *
			 **/
			static int QueryDatabase (const std::string &strConnName, std::string strQueryName, const std::string &strQueryStatement) {
				int iReturn = UNKNOWN_ERROR;
				char *zErrMsg = nullptr;
				if (mapDbConnections.count(strConnName) != 1) {
					iReturn = CONN_NOT_FOUND;
				}
				else if (ClearQueryResults(strQueryName)) {
					// Set the status to Active
					multimapSQLResult[strQueryName]["Metadata"]["Status"] = "Active";
					// Set the number of rows to 0
					multimapSQLResult[strQueryName]["Metadata"]["Rows"] = "0";
					// In the event of a long-running query store the result code of QUERY_ACTIVE to mean Active
					multimapSQLResult[strQueryName]["Metadata"]["ResultCode"] = std::to_string(QUERY_ACTIVE);
					// Execute the SQL Command
					iReturn = sqlite3_exec(mapDbConnections[strConnName], strQueryStatement.c_str(), CallbackSQLite, &strQueryName, &zErrMsg);
					// Store the result code
					multimapSQLResult[strQueryName]["Metadata"]["ResultCode"] = std::to_string(iReturn);
					if (iReturn != SQLITE_OK) {
						if (zErrMsg == nullptr) {
							KnightlyCommon::Log::Debug("Query '" + strQueryStatement + "' Failed: No Error Message");
							multimapSQLResult[strQueryName]["Metadata"]["Status"] = "Failed: No Error Message";
						} else {
							KnightlyCommon::Log::Debug("Query '" + strQueryStatement + "' Failed: " + std::string(zErrMsg));
							multimapSQLResult[strQueryName]["Metadata"]["Status"] = "Failed: " + std::string(zErrMsg);
							sqlite3_free(zErrMsg);
						}
					}
					else {
						KnightlyCommon::Log::Debug("Query '" + strQueryStatement + "' Succeeded!");
						// Set the status to success
						multimapSQLResult[strQueryName]["Metadata"]["Status"] = "Success";
					}

				}
				else {
					iReturn = MAP_NOT_ERASED;
				}
				return iReturn;
			}

			/**
			 * @fn QueryDatabaseWithOutput
			 *
			 * @brief Performs a QueryDatabase call with output to Log functions
			 *
			 * Queries a database and outputs any non-SQLite errors to the error
			 * log.
			 *
			 * @ref QueryDatabase
			 *
			 * @return bool Whether the query was successful or not.
			 *
			 **/
			static bool QueryDatabaseWithOutput (const std::string &strConnName, const std::string &strQueryName, const std::string &strQueryStatement) {
				bool bReturn = false;
				const int rc = QueryDatabase(strConnName, strQueryName, strQueryStatement);

				switch (rc) {
					case CONN_NOT_FOUND:
						KnightlyCommon::Log::Error("Query '" + strQueryName + "' failed, connection doesn't exist: " + strConnName);
						break;
					case MAP_NOT_ERASED:
						KnightlyCommon::Log::Error("Could not clear for Query:" + strQueryName);
						break;
					case UNKNOWN_ERROR:
						KnightlyCommon::Log::Error("Unknown Error in Query:" + strQueryName);
						break;
					case SQLITE_OK:
						bReturn = true;
						break;
					default:
						// The result is stored in the map if we got this far.
						break;
				}

				return bReturn;
			}
	};
}

// NEXTME:  Remove this function since it's better in core
bool ci_equals(std::string& str1, std::string str2)
{
	return ((str1.size() == str2.size()) && std::equal(str1.begin(), str1.end(), str2.begin(), [](char & c1, char & c2){ return (c1 == c2 || std::tolower(c1) == std::tolower(c2)); }));
}

// Define the SQLite Command
PLUGIN_API VOID SQLiteCommand(PSPAWNINFO pSpawn, PCHAR szLine)
{
	std::vector<std::string> vArguments = KnightlyCommon::String::GetArgsFromString(szLine, true, true);
	// If we don't have arguments or the first argument is "help" then show the help info
	if (vArguments.empty() || ci_equals(vArguments[0], "help"))	{
		KnightlySQLite::Log::ShowHelp();
	}
	else {
		// Arg 0 is only unquoted in the deprecated version, but it doesn't hurt to unquote it here
		vArguments[0] = KnightlyCommon::String::UnQuoted(vArguments[0]);
		// Arg 1 is always unquoted as it's either the path, Conn Name, or Query Name (deprecated)
		if (vArguments.size() > 1) {
			vArguments[1] = KnightlyCommon::String::UnQuoted(vArguments[1]);
		}

		if (ci_equals(vArguments[0], "clear")) {
			// Check to make sure we have at least 2 parameters
			if (vArguments.size() > 1) {
				if (!KnightlySQLite::SQL::ClearQueryResults(vArguments[1])) {
					KnightlyCommon::Log::Error("Clear Failed for " + vArguments[1]);
				}
			}
			else {
				KnightlyCommon::Log::Error("Missing parameters - Clear Requires name");
			}
		}
		else if (ci_equals(vArguments[0], "open")) {
			// Check to make sure we have at least 3 parameters
			if (vArguments.size() > 2) {
				// Arg2 is now unquoted as it turns into the path
				vArguments[2] = KnightlyCommon::String::UnQuoted(vArguments[2]);
				if (vArguments.size() > 3) {
					KnightlySQLite::SQL::OpenDatabaseWithOutput(vArguments[1], vArguments[2], vArguments[3]);
				}
				else {
					KnightlySQLite::SQL::OpenDatabaseWithOutput(vArguments[1], vArguments[2]);
				}
			}
			else {
				KnightlyCommon::Log::Error("Missing parameters - Open Requires Name and DB");
			}
		}
		else if (ci_equals(vArguments[0], "close")) {
			// Check to make sure we have at least 2 parameters
			if (vArguments.size() > 1) {
				KnightlySQLite::SQL::CloseDatabaseWithOutput(vArguments[1]);
			}
			else {
				KnightlyCommon::Log::Error("Missing parameters - Close Requires Name");
			}
		}
		else if (ci_equals(vArguments[0], "query") || ci_equals(vArguments[0], "advquery")) {
			// Both types of queries have at least 4 parameters
			if (vArguments.size() > 3) {
				// The 2nd param is the result name in either case
				vArguments[2] = KnightlyCommon::String::UnQuoted(vArguments[2]);
				// Build the SQL query again
				std::string strSQLCommand = vArguments[3];
				for (size_t i = 4; i < vArguments.size(); i++) {
					strSQLCommand += " " + vArguments[i];
				}

				if (ci_equals(vArguments[0], "query")) {
					// Validate that the file path
					if (KnightlySQLite::File::IsValidFilePath(vArguments[1])) {
						// Generate a unique database name
						std::string strGenericDBName = "KnightlyGenericDBCall";
						int i = 1;
						// iterate only 1000 times, if we couldn't get it by then, something went wrong and the OpenDatabase will throw.
						while (KnightlySQLite::mapDbConnections.count(strGenericDBName) != 0 && i < 1000) {
							strGenericDBName = "KnightlyGenericDBCall" + std::to_string(i);
							i++;
						}
						if (KnightlySQLite::SQL::OpenDatabaseWithOutput(strGenericDBName, KnightlySQLite::File::MakeAbsolutePath(vArguments[1]))) {
							KnightlySQLite::SQL::QueryDatabaseWithOutput(strGenericDBName, vArguments[2], strSQLCommand);
							KnightlySQLite::SQL::CloseDatabaseWithOutput(strGenericDBName);
						}
					}
					else {
						KnightlyCommon::Log::Error("Invalid File Path for Query: " + vArguments[1]);
					}
				}
				else {
					KnightlySQLite::SQL::QueryDatabaseWithOutput(vArguments[1], vArguments[2], strSQLCommand);
				}
			}
			else {
				KnightlyCommon::Log::Error("Missing parameters - not enough for a query.");
			}
		}
		else {
			// TODO:  Deprecate this section in favor of leaving just the "Invalid Verb" wording.
			if (KnightlySQLite::warningCountDeprecatedVerb <= KnightlySQLite::MAX_WARNINGS) {
				KnightlyCommon::Log::Warning("Deprecated method or invalid verb.");
				++KnightlySQLite::warningCountDeprecatedVerb;
			}
			// Check to make sure we have at least 3 parameters (if not we don't have enough to process a query)
			if (vArguments.size() > 2) {
				if (KnightlySQLite::warningCountDeprecatedVerb <= KnightlySQLite::MAX_WARNINGS) {
					KnightlyCommon::Log::Warning("Attempting Query - Query verb will be required in a future version.");
					++KnightlySQLite::warningCountDeprecatedVerb;
				}
				// Build the SQL query again
				std::string strSQLCommand = vArguments[2];
				for (size_t i = 3; i < vArguments.size(); i++) {
					strSQLCommand += " " + vArguments[i];
				}

				// Generate a unique database name
				std::string strGenericDBName = "KnightlyGenericDBCall";
				int i = 1;
				// iterate only 1000 times, if we couldn't get it by then, something went wrong and the OpenDatabase will throw.
				while (KnightlySQLite::mapDbConnections.count(strGenericDBName) != 0 && i < 1000) {
					strGenericDBName = "KnightlyGenericDBCall" + std::to_string(i);
					i++;
				}

				if (KnightlySQLite::File::IsValidFilePath(vArguments[0])) {
					if (KnightlySQLite::SQL::OpenDatabaseWithOutput(strGenericDBName, KnightlySQLite::File::MakeAbsolutePath(vArguments[0]))) {
						KnightlySQLite::SQL::QueryDatabaseWithOutput(strGenericDBName, vArguments[1], strSQLCommand);
						KnightlySQLite::SQL::CloseDatabaseWithOutput(strGenericDBName);
					}
				}
				else {
					KnightlyCommon::Log::Error("Invalid File Path for Query: " + vArguments[0]);
				}
			} else {
				KnightlyCommon::Log::Error("Missing parameters for query.");
				KnightlySQLite::Log::ShowHelp();
			}
		}
	}
}

class MQ2SQLiteType *pSQLiteType = nullptr;
class MQ2SQLiteType : public MQ2Type {
	private:
		char _szBuffer[MAX_STRING] = { 0 };
	public:
		enum Members {
			Status,
			Rows,
			Result,
			ResultCode,
			Clear
		};

		MQ2SQLiteType() : MQ2Type("SQLite") {
			TypeMember(Status);
			AddMember(Status, "status");

			TypeMember(Rows);
			AddMember(Rows, "rows");

			TypeMember(Result);
			AddMember(Result, "result");

			TypeMember(ResultCode);
			AddMember(ResultCode, "Resultcode");
			AddMember(ResultCode, "resultCode");
			AddMember(ResultCode, "resultcode");

			TypeMember(Clear);
			AddMember(Clear, "clear");
		}

		bool GetMember(MQ2VARPTR VarPtr, char* Member, char* Index, MQ2TYPEVAR& Dest) {
			_szBuffer[0] = '\0';
			// Query / Row / Column
			const std::vector<std::string> vArgs = KnightlyCommon::String::GetArgsFromString(Index);
			const int argQueryName = 0;
			const int argRow = 1;
			const int argColumn = 2;

			PMQ2TYPEMEMBER pMember = MQ2SQLiteType::FindMember(Member);
			if (!pMember) return false;

			switch (pMember->ID) {
				case Status:
					Dest.Type = pStringType;
					// If we have a status set ...
					if (KnightlySQLite::multimapSQLResult[Index]["Metadata"].count("Status") == 1)
					{
						strcpy_s(_szBuffer, KnightlySQLite::multimapSQLResult[Index]["Metadata"]["Status"].c_str());
						Dest.Ptr = &_szBuffer[0];
						return true;
					} else {
						// If we don't have a status, just return null...
						strcpy_s(_szBuffer, "NULL");
						Dest.Ptr = &_szBuffer[0];
						return true;
					}
				case Rows:
					Dest.Type = pIntType;
					if (KnightlySQLite::multimapSQLResult[Index]["Metadata"].count("Rows") == 1)
					{
						// NEXTME: Dest.Int = GetIntFromString(KnightlySQLite::multimapSQLResult[Index]["Metadata"]["Rows"], 0);
						Dest.Int = std::stoi(KnightlySQLite::multimapSQLResult[Index]["Metadata"]["Rows"]);
					} else {
						Dest.Int = 0;
					}
					return true;
				case Result:
					Dest.Type = pStringType;
					// Make sure we have three parameters
					if (vArgs.size() == 3) {
						// Make sure we have that Query
						if (KnightlySQLite::multimapSQLResult.count(vArgs[argQueryName]) == 1) {
							// Make sure the query is complete
							if (KnightlySQLite::multimapSQLResult[vArgs[argQueryName]]["Metadata"]["Status"] == "Success") {
								// Make sure the row exists
								if (KnightlySQLite::multimapSQLResult[vArgs[argQueryName]].count(vArgs[argRow])) {
									// Make sure the column exists
									if (KnightlySQLite::multimapSQLResult[vArgs[argQueryName]][vArgs[argRow]].count(vArgs[argColumn])) {
										// Return whatever they asked for
										strcpy_s(_szBuffer, (KnightlySQLite::multimapSQLResult[vArgs[argQueryName]][vArgs[argRow]][vArgs[argColumn]]).c_str());
									} else {
										KnightlyCommon::Log::Error("Column " + std::string(vArgs[argColumn]) + " does not exist.");
										strcpy_s(_szBuffer, ("Failure:  Column: " + std::string(vArgs[argColumn]) + " does not exist.").c_str());
									}
								} else {
									KnightlyCommon::Log::Debug("Row Count: " + std::to_string(KnightlySQLite::multimapSQLResult[vArgs[argQueryName]].count(vArgs[argRow])));
									KnightlyCommon::Log::Error("Row " + std::string(vArgs[argRow]) + " does not exist.");
									strcpy_s(_szBuffer, ("Failure:  Row: " + std::string(vArgs[argRow]) + " does not exist.").c_str());
								}
							} else {
								strcpy_s(_szBuffer, ("Failure:  Cannot get results, status is: " + KnightlySQLite::multimapSQLResult[vArgs[argQueryName]]["Metadata"]["Status"]).c_str());
							}
						} else {
							KnightlyCommon::Log::Error("Query " + std::string(vArgs[argQueryName]) + " does not exist.");
							strcpy_s(_szBuffer, ("Failure:  " + std::string(vArgs[argQueryName]) + " does not exist.").c_str());
						}
					} else {
						KnightlyCommon::Log::Error("Result Call requires exactly three parameters ${sqlite.result[QueryName RowNumber ColumnName]}.");
						strcpy_s(_szBuffer, "Failure:  Need 3 parameters");
					}
					Dest.Ptr = &_szBuffer[0];
					return true;
				case ResultCode:
					Dest.Type = pIntType;
					// If we have a ResultCode
					if (KnightlySQLite::multimapSQLResult[Index]["Metadata"].count("ResultCode") == 1)
					{
						// NEXTME: Dest.Int = GetIntFromString(KnightlySQLite::multimapSQLResult[Index]["Metadata"]["ResultCode"], KnightlySQLite::SQL::custom_sqlite_result::CONVERSION_ERROR);
						Dest.Int = std::stoi(KnightlySQLite::multimapSQLResult[Index]["Metadata"]["ResultCode"]);
					}
					else {
						Dest.Int = KnightlySQLite::SQL::custom_sqlite_result::QUERY_NOT_FOUND;
					}
					return true;
				// TODO: Deprecate this section.
				case Clear:
					if (KnightlySQLite::warningCountDotClear <= KnightlySQLite::MAX_WARNINGS) {
						KnightlyCommon::Log::Warning(".clear TLO is deprecated and will be removed in a future version.");
						++KnightlySQLite::warningCountDotClear;
					}
					Dest.Type = pBoolType;
					Dest.Int = KnightlySQLite::SQL::ClearQueryResults(Index);
					return true;
				default:
					break;
			}
			return false;
		}

		bool FromData(MQ2VARPTR& VarPtr, MQ2TYPEVAR& Source) { return false; }
		bool FromString(MQ2VARPTR& VarPtr, char* Source) { return false; }
};

BOOL SQLiteData(PCHAR szIndex, MQ2TYPEVAR& Dest)
{
	Dest.DWord = 1;
	Dest.Type = pSQLiteType;
	return TRUE;
}

// Called once, when the plugin is to initialize
PLUGIN_API VOID InitializePlugin()
{
	DebugSpewAlways("Initializing MQ2SQLite");
	KnightlyCommon::pluginName = "MQ2SQLite";
	// Add /sqlite
	AddCommand("/sqlite", SQLiteCommand);
	// Add a data type to handle results
	pSQLiteType = new MQ2SQLiteType;
	AddMQ2Data("sqlite", SQLiteData);
}

// Called once, when the plugin is to shutdown
PLUGIN_API VOID ShutdownPlugin()
{
	DebugSpewAlways("Shutting down MQ2SQLite");
	// Close all of the database connections gracefully (and quietly)
	/* NEXTME:
	for (auto const &[key, val] : KnightlySQLite::mapDbConnections) {
		if (KnightlySQLite::SQL::CloseDatabase(key) != 0) {
			KnightlyCommon::Log::Error("Could not gracefully close database (" + key +") please remember to close your connections before unloading.");
		}
		 */
	for (auto const& mapentry : KnightlySQLite::mapDbConnections) {
		if (KnightlySQLite::SQL::CloseDatabase(mapentry.first) != 0) {
			KnightlyCommon::Log::Error("Could not gracefully close database (" + mapentry.first +") please remember to close your connections before unloading.");
		}
	}
	// Remove /sqlite
	RemoveCommand("/sqlite");
	// Remove data type
	RemoveMQ2Data("sqlite");
	delete pSQLiteType;
}
