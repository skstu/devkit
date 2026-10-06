#include <libsys/resources.h>
#include <uv.h>
#if defined(__APPLE__)
#include <mach/mach.h>
#endif
#if !defined(_WIN32)
#include <dirent.h>
#include <sys/resource.h>
#endif
namespace libsys {
    std::uint64_t AvailableMemory() {
#if defined(__APPLE__)
        // libuv's Darwin free count omits reclaimable inactive pages. Treating
        // file cache as exhausted RAM makes merely launching Chrome collapse the
        // admission budget. Read VM counters; never purge caches or alter the OS.
        vm_statistics64_data_t stats{};
        mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
        vm_size_t page = 0;
        const auto host = mach_host_self();
        const bool ok = host_page_size(host, &page) == KERN_SUCCESS &&
                        host_statistics64(host, HOST_VM_INFO64,
                                          reinterpret_cast<host_info64_t>(&stats),
                                          &count) == KERN_SUCCESS;
        mach_port_deallocate(mach_task_self(), host);
        if (ok)
            return (std::uint64_t(stats.free_count) + stats.inactive_count) * page;
#endif
#if defined(__ANDROID__)
        // Android can deny /proc/self/cgroup; libuv's available-memory query then
        // returns zero. Its free-memory query reads MemAvailable directly.
        return uv_get_free_memory();
#else
        return uv_get_available_memory();
#endif
    }
    SystemResources InspectSystemResources() {
        SystemResources snapshot;
        snapshot.available_memory = AvailableMemory();
#if !defined(_WIN32)

        rlimit descriptors{};
        if (getrlimit(RLIMIT_NOFILE, &descriptors) == 0)
            snapshot.descriptor_limit = descriptors.rlim_cur;
#if defined(__linux__) || defined(__ANDROID__)
        auto* directory = opendir("/proc/self/fd");
#else
        auto* directory = opendir("/dev/fd");
#endif
        if (directory) {
            std::uint64_t used = 0;
            while (const auto* entry = readdir(directory))
                if (entry->d_name[0] != '.')
                    ++used;
            closedir(directory);
            snapshot.descriptors_used = used;
        }

#endif
        return snapshot;
    }
}
