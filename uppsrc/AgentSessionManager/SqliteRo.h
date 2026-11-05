#ifndef _AgentSessionManager_SqliteRo_h_
#define _AgentSessionManager_SqliteRo_h_

#include <Core/Core.h>

typedef struct sqlite3 sqlite3;
typedef struct sqlite3_stmt sqlite3_stmt;

NAMESPACE_UPP

class SqliteRo {
	sqlite3 *db;

public:
	SqliteRo() : db(NULL) {}
	~SqliteRo() { Close(); }

	bool   Open(const String& path);
	void   Close();
	bool   IsOpen() const          { return db != NULL; }
	sqlite3 *Handle()              { return db; }
	String Error() const;

private:
	SqliteRo(const SqliteRo&);
	SqliteRo& operator=(const SqliteRo&);
};

enum SqlStepResult { SQLITE_STEP_ROW = 1, SQLITE_STEP_DONE = 2, SQLITE_STEP_ERROR = 3 };

class SqliteStatement {
	sqlite3      *db;
	sqlite3_stmt *stmt;
	String        err;

public:
	SqliteStatement() : db(NULL), stmt(NULL) {}
	~SqliteStatement() { Finalize(); }

	bool Prepare(SqliteRo& ro, const String& sql);
	bool Prepared() const          { return stmt != NULL; }
	void BindText(int i, const String& v);
	void BindInt64(int i, int64 v);
	SqlStepResult Step();
	String Text(int col);
	int64  Int(int col);
	String Blob(int col);
	bool   IsNull(int col) const;
	void   Finalize();
	String Error() const           { return err; }

private:
	SqliteStatement(const SqliteStatement&);
	SqliteStatement& operator=(const SqliteStatement&);
};

END_UPP_NAMESPACE

#endif
