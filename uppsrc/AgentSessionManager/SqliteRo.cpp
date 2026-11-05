#include "AgentSessionManager.h"
#include <sqlite3.h>

NAMESPACE_UPP

bool SqliteRo::Open(const String& path)
{
	Close();
	String uri = "file:" + path + "?mode=ro";
	int rc = sqlite3_open_v2(~uri, &db, SQLITE_OPEN_READONLY | SQLITE_OPEN_URI, NULL);
	if(rc != SQLITE_OK) {
		if(db) {
			sqlite3_close(db);
			db = NULL;
		}
		return false;
	}
	sqlite3_busy_timeout(db, 2000);
	return true;
}

void SqliteRo::Close()
{
	if(db) {
		sqlite3_close(db);
		db = NULL;
	}
}

String SqliteRo::Error() const
{
	if(!db)
		return "unable to open database file";
	const char *msg = sqlite3_errmsg(db);
	return msg ? String(msg) : String("unknown sqlite error");
}

bool SqliteStatement::Prepare(SqliteRo& ro, const String& sql)
{
	Finalize();
	db = ro.Handle();
	if(!db) {
		err = "database is not open";
		return false;
	}
	int rc = sqlite3_prepare_v2(db, ~sql, sql.GetCount(), &stmt, NULL);
	if(rc != SQLITE_OK) {
		err = ro.Error();
		stmt = NULL;
		return false;
	}
	err.Clear();
	return true;
}

void SqliteStatement::BindText(int i, const String& v)
{
	if(stmt)
		sqlite3_bind_text(stmt, i, ~v, v.GetCount(), SQLITE_TRANSIENT);
}

void SqliteStatement::BindInt64(int i, int64 v)
{
	if(stmt)
		sqlite3_bind_int64(stmt, i, v);
}

SqlStepResult SqliteStatement::Step()
{
	if(!stmt) {
		if(err.IsEmpty())
			err = "statement is not prepared";
		return SQLITE_STEP_ERROR;
	}
	int rc = sqlite3_step(stmt);
	if(rc == SQLITE_ROW)
		return SQLITE_STEP_ROW;
	if(rc == SQLITE_DONE)
		return SQLITE_STEP_DONE;
	err = db ? String(sqlite3_errmsg(db)) : String("sqlite error");
	return SQLITE_STEP_ERROR;
}

String SqliteStatement::Text(int col)
{
	if(!stmt)
		return String();
	const unsigned char *p = sqlite3_column_text(stmt, col);
	if(!p)
		return String();
	int n = sqlite3_column_bytes(stmt, col);
	return String((const char *)p, n);
}

int64 SqliteStatement::Int(int col)
{
	return stmt ? sqlite3_column_int64(stmt, col) : 0;
}

String SqliteStatement::Blob(int col)
{
	if(!stmt)
		return String();
	const void *p = sqlite3_column_blob(stmt, col);
	int n = sqlite3_column_bytes(stmt, col);
	if(!p || n <= 0)
		return String();
	return String((const char *)p, n);
}

bool SqliteStatement::IsNull(int col) const
{
	return !stmt || sqlite3_column_type(stmt, col) == SQLITE_NULL;
}

void SqliteStatement::Finalize()
{
	if(stmt) {
		sqlite3_finalize(stmt);
		stmt = NULL;
	}
	db = NULL;
	err.Clear();
}

END_UPP_NAMESPACE
