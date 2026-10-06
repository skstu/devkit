#include <libdb/libdb.h>
#include <sqlite3.h>
#include <limits>
namespace libdb {
    int Execute(sqlite3* db, const char* sql) noexcept {
        return sqlite3_exec(db, sql, nullptr, nullptr, nullptr);
    }
    Statement::~Statement() {
        sqlite3_finalize(value);
    }
    int Statement::Prepare(sqlite3* db, std::string_view sql) noexcept {
        if (sql.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
            return SQLITE_TOOBIG;
        sqlite3_finalize(value);
        value = nullptr;
        return sqlite3_prepare_v2(db, sql.data(), static_cast<int>(sql.size()), &value, nullptr);
    }
    int Statement::BindText(int index, std::string_view text) noexcept {
        if (text.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
            return SQLITE_TOOBIG;
        return sqlite3_bind_text(value, index, text.empty() ? "" : text.data(), static_cast<int>(text.size()), SQLITE_TRANSIENT);
    }
    int Statement::BindInt64(int index, std::int64_t number) noexcept {
        return sqlite3_bind_int64(value, index, number);
    }
    int Statement::BindBlob(int index, const void* bytes, std::size_t size) noexcept {
        if (size > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
            return SQLITE_TOOBIG;
        return sqlite3_bind_blob(value, index, bytes, static_cast<int>(size), SQLITE_TRANSIENT);
    }
    int Statement::Step() noexcept {
        return sqlite3_step(value);
    }
    Transaction::Transaction(sqlite3* db, bool clear_progress_on_rollback) noexcept : db_(db), clear_progress_(clear_progress_on_rollback) {
    }
    Transaction::~Transaction() {
        if (active_) {
            if (clear_progress_)
                sqlite3_progress_handler(db_, 0, nullptr, nullptr);
            Execute(db_, "ROLLBACK");
        }
    }
    int Transaction::Begin() noexcept {
        if (active_)
            return SQLITE_MISUSE;
        const auto rc = Execute(db_, "BEGIN IMMEDIATE");
        if (rc == SQLITE_OK)
            active_ = true;
        return rc;
    }
    int Transaction::Commit() noexcept {
        if (!active_)
            return SQLITE_MISUSE;
        const auto rc = Execute(db_, "COMMIT");
        if (rc == SQLITE_OK)
            active_ = false;
        return rc;
    }
}
