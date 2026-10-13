#pragma once
#include "libnet/net.h"
#include "libnet_uv.h"
#include <vector>
namespace libnet::detail {
struct LanPath { dknet_interface row{}; uint64_t android_network=0; bool preferred=false; };
bool MatchesLanInterface(const dknet_interface&, const char*);
uint32_t InterfaceFlags(const char *name);
bool SyntheticAddress(const char *address);
int ListLanPaths(const char *explicit_address, size_t maximum, std::vector<LanPath>&);
const LanPath *SelectLanPath(const std::vector<LanPath>&, const Endpoint&);
bool PrepareLanSocket(const LanPath&, uintptr_t socket);
int AndroidInitialize(void *environment, void *context);
}
