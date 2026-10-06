#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>
struct sqlite3;
struct sqlite3_stmt;
namespace libdb {
    // Thin SQLCipher/SQLite resource wrappers. Return native result codes; schema,
    // authorization, retry/deadline and result interpretation belong to callers.
    int Execute(sqlite3* db, const char* sql) noexcept;
    class Statement {
    public:
        sqlite3_stmt* value = nullptr;
        Statement() = default;
        ~Statement();
        Statement(const Statement&) = delete;
        Statement& operator=(const Statement&) = delete;
        int Prepare(sqlite3* db, std::string_view sql) noexcept;
        int BindText(int index, std::string_view value) noexcept;
        int BindInt64(int index, std::int64_t value) noexcept;
        int BindBlob(int index, const void* bytes, std::size_t size) noexcept;
        int Step() noexcept;
    };
    class Transaction {
    public:
        // Opt in only when this owner also owns the connection's progress handler.
        explicit Transaction(sqlite3* db, bool clear_progress_on_rollback = false) noexcept;
        ~Transaction();
        Transaction(const Transaction&) = delete;
        Transaction& operator=(const Transaction&) = delete;
        int Begin() noexcept;
        int Commit() noexcept;

    private:
        sqlite3* db_;
        bool active_ = false, clear_progress_;
    };
}
