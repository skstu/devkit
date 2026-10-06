#include <libsys.h>
#include "../device_fp.hpp"

#include <iphlpapi.h>
#include <intrin.h>
#pragma comment(lib, "iphlpapi.lib")

// ─── helpers ──────────────────────────────────────────────────────────────────

static std::string reg_read_string(HKEY root, const char* subkey,
                                   const char* value) {
  HKEY hk = nullptr;
  if (RegOpenKeyExA(root, subkey, 0, KEY_READ | KEY_WOW64_64KEY, &hk) !=
      ERROR_SUCCESS)
    return {};
  char buf[512] = {};
  DWORD sz = sizeof(buf);
  DWORD type = 0;
  LONG rc = RegQueryValueExA(hk, value, nullptr, &type,
                             reinterpret_cast<LPBYTE>(buf), &sz);
  RegCloseKey(hk);
  if (rc != ERROR_SUCCESS) return {};
  return std::string(buf);
}

// Read the SMBIOS table and return the value of the first field that matches
// 'type' and 'field_offset' (byte offset inside the fixed-length structure).
static std::string smbios_string(BYTE struct_type, size_t field_offset) {
  DWORD sz = GetSystemFirmwareTable('RSMB', 0, nullptr, 0);
  if (!sz) return {};
  std::vector<BYTE> buf(sz);
  if (GetSystemFirmwareTable('RSMB', 0, buf.data(), sz) != sz) return {};

  // Raw SMBIOS data layout: 8-byte header then the table data.
  const BYTE* p   = buf.data() + 8;
  const BYTE* end = buf.data() + sz;
  while (p + 4 < end) {
    BYTE type   = p[0];
    BYTE length = p[1];
    if (p + length > end) break;
    if (type == struct_type && (size_t)length > field_offset) {
      BYTE str_idx = p[field_offset];
      if (str_idx > 0) {
        // string section follows the fixed-length part
        const char* s = reinterpret_cast<const char*>(p + length);
        for (BYTE i = 1; i < str_idx && s < reinterpret_cast<const char*>(end); ++i) {
          s += std::strlen(s) + 1;
        }
        if (s < reinterpret_cast<const char*>(end))
          return std::string(s);
      }
    }
    // Skip to next structure: fixed part + string set (double-NUL terminated)
    const BYTE* strings = p + length;
    while (strings + 1 < end &&
           !(strings[0] == 0 && strings[1] == 0))
      ++strings;
    p = strings + 2;
  }
  return {};
}

// Return MAC of the first "physical" adapter (skips loopback & tunnel types).
static std::string first_mac_address() {
  ULONG sz = 0;
  GetAdaptersAddresses(AF_UNSPEC,
    GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
    GAA_FLAG_SKIP_DNS_SERVER, nullptr, nullptr, &sz);
  if (!sz) return {};
  std::vector<BYTE> buf(sz);
  auto* p = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data());
  if (GetAdaptersAddresses(AF_UNSPEC,
        GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
        GAA_FLAG_SKIP_DNS_SERVER, nullptr, p, &sz) != NO_ERROR)
    return {};
  for (; p; p = p->Next) {
    if (p->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
    if (p->IfType == IF_TYPE_TUNNEL)            continue;
    if (p->PhysicalAddressLength != 6)          continue;
    char mac[18];
    std::snprintf(mac, sizeof(mac), "%02x:%02x:%02x:%02x:%02x:%02x",
      p->PhysicalAddress[0], p->PhysicalAddress[1],
      p->PhysicalAddress[2], p->PhysicalAddress[3],
      p->PhysicalAddress[4], p->PhysicalAddress[5]);
    return std::string(mac);
  }
  return {};
}

// ─── implementation ───────────────────────────────────────────────────────────

std::string ISystem::GetDeviceFingerprint() {
  std::vector<std::string> parts;
  parts.reserve(7);

  // 1. Windows MachineGuid – survives reboots and driver reinstalls
  parts.push_back(reg_read_string(
    HKEY_LOCAL_MACHINE,
    "SOFTWARE\\Microsoft\\Cryptography",
    "MachineGuid"));

  // 2. CPU vendor + family/model/stepping
  {
    int info[4] = {};
    __cpuid(info, 0);
    char vendor[13] = {};
    std::memcpy(vendor + 0, &info[1], 4);
    std::memcpy(vendor + 4, &info[3], 4);
    std::memcpy(vendor + 8, &info[2], 4);
    __cpuid(info, 1);
    char cpu[32];
    std::snprintf(cpu, sizeof(cpu), "%s-%08x", vendor, (unsigned)info[0]);
    parts.push_back(cpu);
  }

  // 3. System-drive volume serial number
  {
    char windir[MAX_PATH] = {};
    GetWindowsDirectoryA(windir, MAX_PATH);
    windir[3] = '\0'; // keep "C:\\"
    DWORD volser = 0;
    GetVolumeInformationA(windir, nullptr, 0, &volser, nullptr, nullptr,
                          nullptr, 0);
    char vbuf[16];
    std::snprintf(vbuf, sizeof(vbuf), "%08x", volser);
    parts.push_back(vbuf);
  }

  // 4. SMBIOS Baseboard serial number (type 2, offset 7)
  parts.push_back(smbios_string(2, 7));

  // 5. SMBIOS System UUID (type 1, offset 8) – raw string index
  parts.push_back(smbios_string(1, 8));

  // 6. First physical MAC address
  parts.push_back(first_mac_address());

  // 7. CPU processor ID from SMBIOS (type 4, offset 8)
  parts.push_back(smbios_string(4, 8));

  return device_fp::make_fingerprint(parts);
}
