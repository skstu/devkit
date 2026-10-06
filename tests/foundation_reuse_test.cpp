#include <libstl/hex.hpp>
#include <libstl/text.hpp>
#include <libstl/binary.hpp>
#include <libstl/clock.hpp>
#include <libstl/executor.hpp>
#include <libstl/byte_budget.hpp>
#include <libcompr/stream.hpp>
#include <libnet_dns.h>
#include <libcrypt.h>
#include <array>
#include <atomic>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
using namespace std::chrono_literals;
void Check(bool ok) {
    if (!ok)
        throw std::runtime_error("foundation contract failed");
}
void WalkTreeContract() {
    namespace fs = std::filesystem;
    struct Fixture {
        fs::path root = fs::temp_directory_path() /
            ("sovkit-walk-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        Fixture() { Check(fs::create_directory(root)); }
        ~Fixture() { std::error_code ignored; fs::remove_all(root, ignored); }
    } fixture;
    const auto tree = fixture.root / "tree";
    fs::create_directory(tree);
    std::ofstream(tree / "file") << "contents";
    int visited = 0;
    const auto visitor = [&](const fs::directory_entry&, bool) { ++visited; return true; };
    Check(libcompr::WalkTree(tree, visitor) == libcompr::WalkStatus::Ok && visited == 1);
    Check(libcompr::WalkTree(tree, visitor, [] { return true; }) == libcompr::WalkStatus::Canceled);
    Check(libcompr::WalkTree(tree / "file", visitor) == libcompr::WalkStatus::InvalidSource);
    const auto link = fixture.root / "link";
    std::error_code error;
    fs::create_directory_symlink(tree, link, error);
#ifdef _WIN32
    if (error == std::errc::operation_not_permitted || error.value() == ERROR_PRIVILEGE_NOT_HELD) {
        std::cout << "SKIP WalkTree symlinks: Windows symlink privilege unavailable\n";
        return;
    }
#endif
    Check(!error);
    visited = 0;
    Check(libcompr::WalkTree(link, visitor) == libcompr::WalkStatus::InvalidSource && visited == 0);
    Check(libcompr::WalkTree(link / "", visitor) == libcompr::WalkStatus::InvalidSource && visited == 0);
    Check(libcompr::WalkTree(link / ".", visitor) == libcompr::WalkStatus::InvalidSource && visited == 0);
    fs::remove(link);
    fs::create_directory_symlink(tree, tree / "loop");
    Check(libcompr::WalkTree(tree, visitor) == libcompr::WalkStatus::InvalidSource);
}
int main() {
    try {
        WalkTreeContract();
        std::array<std::uint8_t, 2> bytes{0x55, 0x55};
        Check(!stl::Unhex("aaff0", bytes) && bytes[0] == 0x55);
        Check(!stl::Unhex("AAff", bytes) && bytes[1] == 0x55);
        Check(!stl::Unhex("aaz0", bytes) && bytes[0] == 0x55);
        Check(stl::Unhex("AAff", bytes, stl::HexCase::Either) && stl::Hex(bytes) == "aaff");
        Check(stl::Unhex("", {}) && stl::IsValidUtf8("\xf0\x9f\x98\x80"));
        for (auto invalid : {"\xc0\x80", "\xed\xa0\x80", "\xf4\x90\x80\x80", "\xe2\x82"})
            Check(!stl::IsValidUtf8(invalid));
        std::vector<std::uint8_t> wire;
        stl::binary::AppendU16(wire, 0x1234);
        stl::binary::AppendU64(wire, 0x0102030405060708ULL);
        Check(stl::Hex(wire) == "12340102030405060708");
        stl::binary::Reader reader(wire);
        std::uint16_t u16 = 0;
        std::uint64_t u64 = 0;
        Check(reader.U16(u16) && u16 == 0x1234 && reader.U64(u64) && u64 == 0x0102030405060708ULL);
        Check(!reader.U16(u16) && u16 == 0x1234 && reader.remaining() == 0);
        Check(stl::UnixSeconds() > 1000000000 && stl::UnixMilliseconds() / 1000 >= stl::UnixSeconds() - 1);
        std::future<int> finished;
        std::atomic<int> completions{0};
        {
            stl::BoundedExecutor executor(1, 2);
            finished = executor.Submit([] { return 7; }, [&] { ++completions; });
        }
        Check(finished.get() == 7 && completions == 1);
        {
            stl::BoundedExecutor executor(1, 1);
            std::promise<void> started, release;
            auto gate = release.get_future().share();
            auto active = executor.Submit([&] { started.set_value(); gate.wait(); });
            started.get_future().wait();
            auto queued = executor.Submit([] { throw std::runtime_error("job"); });
            bool full = false;
            try {
                executor.Submit([] {});
            }
            catch (const std::length_error&) {
                full = true;
            }
            release.set_value();
            active.get();
            executor.WaitForIdle();
            Check(full);
            bool propagated = false;
            try {
                queued.get();
            }
            catch (const std::runtime_error&) {
                propagated = true;
            }
            Check(propagated);
        }
        {
            auto executor = std::make_unique<stl::BoundedExecutor>(1, 2, stl::BoundedExecutor::Shutdown::CancelPending);
            std::promise<void> started, release;
            auto gate = release.get_future().share();
            auto active = executor->Submit([&] { started.set_value(); gate.wait(); });
            started.get_future().wait();
            auto canceled = executor->Submit([] { return 42; });
            std::thread destroy([owned = std::move(executor)]() mutable { owned.reset(); });
            const auto ready = canceled.wait_for(3s);
            release.set_value();
            destroy.join();
            active.get();
            Check(ready == std::future_status::ready);
            bool broken = false;
            try {
                (void)canceled.get();
            }
            catch (const std::future_error&) {
                broken = true;
            }
            Check(broken);
        }
        static_assert(!std::is_copy_constructible_v<stl::ByteBudget::Lease>);
        auto budget = std::make_shared<stl::ByteBudget>(8);
        auto held = budget->Reserve(8);
        Check(held && !budget->Reserve(1));
        Check(!budget->Acquire("first", 4) && !budget->Acquire("second", 4));
        held.reset();
        Check(!budget->Acquire("second", 4));
        auto first = budget->Acquire("first", 4), second = budget->Acquire("second", 4);
        Check(first && second && budget->Inspect().bytes == 8);
        first.reset();
        second.reset();
        Check(budget->Inspect().bytes == 0);
        std::array<std::uint8_t, 3> buffer{};
        std::istringstream input("abcdef");
        std::ostringstream output;
        Check(libcompr::CopyExactly(input, output, 6, buffer) && output.str() == "abcdef");
        std::istringstream short_input("ab");
        Check(!libcompr::CopyExactly(short_input, output, 3, buffer));
        std::istringstream canceled_input("abc");
        Check(!libcompr::CopyExactly(canceled_input, output, 3, buffer, {}, [] { return true; }));
        std::vector<std::uint8_t> dns;
        Check(libnet::dns::AppendName(dns, "a.local"));
        auto offset = dns.size();
        dns.push_back(0xc0);
        dns.push_back(0);
        std::string name;
        Check(libnet::dns::ReadName(dns, offset, name, 32) && name == "a.local" && offset == dns.size());
        std::array<std::uint8_t, 2> cycle{0xc0, 0};
        offset = 0;
        Check(!libnet::dns::ReadName(cycle, offset, name, 32) && offset == 0);
        Crypt::Ed25519Seed seed{};
        Crypt::Ed25519PublicKey public_key{};
        Crypt::Ed25519Signature signature{};
        Check(Crypt::RandomFill(seed) && Crypt::Ed25519PublicFromSeed(seed, public_key));
        Check(Crypt::Ed25519SignFromSeed(seed, wire, signature) && Crypt::Ed25519Verify(wire, signature, public_key));
        std::cout << "foundation contracts passed\n";
        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
